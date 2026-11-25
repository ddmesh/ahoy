//-----------------------------------------------------------------------------
// 2024 Ahoy, https://ahoydtu.de
// Creative Commons - https://creativecommons.org/licenses/by-nc-sa/4.0/deed
//-----------------------------------------------------------------------------
// Minimal read-only Modbus TCP server (function codes 0x03 and 0x04)
// used by the SunSpec plugin. Only compiled with ENABLE_SUNSPEC.
//-----------------------------------------------------------------------------

#ifndef __MODBUS_TCP_SERVER_H__
#define __MODBUS_TCP_SERVER_H__

#include "../../config/config.h" // ENABLE_SUNSPEC switch

#if defined(ENABLE_SUNSPEC)

#include <Arduino.h>
#include <functional>
#include <new>
#if defined(ESP32)
    #include <AsyncTCP.h>
#else
    #include <ESPAsyncTCP.h>
#endif
#include "../../utils/dbg.h"

#define MODBUS_MAX_ALLOW_ENTRIES    4
#define MODBUS_MAX_READ_REGS        125
#define MODBUS_RX_BUF_SIZE          260
#define MODBUS_CLIENT_TIMEOUT_SEC   60

// Modbus exception codes
#define MB_EX_ILLEGAL_FUNCTION      0x01
#define MB_EX_ILLEGAL_ADDRESS       0x02
#define MB_EX_ILLEGAL_VALUE         0x03
#define MB_EX_GW_TARGET_FAILED      0x0B

class ModbusTcpServer {
    public:
        // returns 0 on success, otherwise a Modbus exception code
        // dst holds 'cnt' registers in host byte order
        typedef std::function<uint8_t(uint8_t unitId, uint16_t addr, uint16_t cnt, uint16_t *dst)> readCb;

        ModbusTcpServer() {}

        ~ModbusTcpServer() {
            if(nullptr != mServer) {
                mServer->end();
                delete mServer;
            }
        }

        // allowList: comma separated list of IPv4 addresses or networks,
        // e.g. "192.168.1.10, 192.168.2.0/24". Empty string allows all.
        bool begin(uint16_t port, uint8_t maxClients, const char *allowList, readCb cb) {
            if(nullptr != mServer)
                return false;

            mCb         = cb;
            mMaxClients = (0 == maxClients) ? 1 : maxClients;
            parseAllowList(allowList);

            mServer = new AsyncServer(port);
            mServer->onClient([this](void *, AsyncClient *client) { onConnect(client); }, nullptr);
            mServer->setNoDelay(true);
            mServer->begin();

            DPRINT(DBG_INFO, F("Modbus TCP server listening on port "));
            DBGPRINTLN(String(port));
            return true;
        }

        uint8_t getNumClients(void) const {
            return mNumClients;
        }

        uint32_t getRequestCnt(void) const {
            return mReqCnt;
        }

        uint32_t getRejectCnt(void) const {
            return mRejectCnt;
        }

    private:
        struct ClientCtx {
            ModbusTcpServer *srv;
            uint16_t len;
            uint8_t buf[MODBUS_RX_BUF_SIZE];
        };

        struct AllowEntry {
            uint32_t ip;
            uint32_t mask;
        };

        static void dropClient(AsyncClient *client) {
            // the server allocated the client, free it once it is closed
            client->onDisconnect([](void *, AsyncClient *c) { delete c; }, nullptr);
            client->close(true);
        }

        void onConnect(AsyncClient *client) {
            if(!isAllowed(client->remoteIP())) {
                mRejectCnt++;
                DPRINT(DBG_INFO, F("Modbus: rejected "));
                DBGPRINTLN(client->remoteIP().toString());
                dropClient(client);
                return;
            }
            if(mNumClients >= mMaxClients) {
                mRejectCnt++;
                DPRINTLN(DBG_INFO, F("Modbus: max clients reached"));
                dropClient(client);
                return;
            }

            ClientCtx *ctx = new (std::nothrow) ClientCtx;
            if(nullptr == ctx) {
                dropClient(client);
                return;
            }
            ctx->srv = this;
            ctx->len = 0;
            mNumClients++;

            client->setRxTimeout(MODBUS_CLIENT_TIMEOUT_SEC);
            client->onData([](void *arg, AsyncClient *c, void *data, size_t len) {
                ClientCtx *ctx = static_cast<ClientCtx*>(arg);
                ctx->srv->onData(ctx, c, static_cast<uint8_t*>(data), len);
            }, ctx);
            client->onDisconnect([](void *arg, AsyncClient *c) {
                ClientCtx *ctx = static_cast<ClientCtx*>(arg);
                if(ctx->srv->mNumClients > 0)
                    ctx->srv->mNumClients--;
                delete ctx;
                delete c;
            }, ctx);
            client->onTimeout([](void *, AsyncClient *c, uint32_t) {
                c->close(true);
            }, ctx);
        }

        void onData(ClientCtx *ctx, AsyncClient *c, uint8_t *data, size_t len) {
            while(len > 0) {
                size_t n = MODBUS_RX_BUF_SIZE - ctx->len;
                if(n > len)
                    n = len;
                memcpy(&ctx->buf[ctx->len], data, n);
                ctx->len += n;
                data     += n;
                len      -= n;

                // process all complete frames in buffer
                while(ctx->len >= 8) {
                    uint16_t pduLen = (ctx->buf[4] << 8) | ctx->buf[5]; // unit id + pdu
                    if((pduLen < 2) || (pduLen > (MODBUS_RX_BUF_SIZE - 6)) || (0 != ctx->buf[2]) || (0 != ctx->buf[3])) {
                        c->close(true); // garbage, protocol id must be 0
                        return;
                    }
                    uint16_t frameLen = 6 + pduLen;
                    if(ctx->len < frameLen)
                        break;

                    handleFrame(c, ctx->buf, frameLen);

                    ctx->len -= frameLen;
                    if(ctx->len > 0)
                        memmove(ctx->buf, &ctx->buf[frameLen], ctx->len);
                }
                if(MODBUS_RX_BUF_SIZE == ctx->len) { // overflow
                    c->close(true);
                    return;
                }
            }
        }

        void handleFrame(AsyncClient *c, const uint8_t *req, uint16_t len) {
            mReqCnt++;
            uint8_t unitId = req[6];
            uint8_t fc     = req[7];

            if((0x03 != fc) && (0x04 != fc)) {
                sendException(c, req, MB_EX_ILLEGAL_FUNCTION);
                return;
            }
            if(len < 12) {
                sendException(c, req, MB_EX_ILLEGAL_VALUE);
                return;
            }

            uint16_t addr = (req[8]  << 8) | req[9];
            uint16_t cnt  = (req[10] << 8) | req[11];
            if((0 == cnt) || (cnt > MODBUS_MAX_READ_REGS)) {
                sendException(c, req, MB_EX_ILLEGAL_VALUE);
                return;
            }

            uint8_t ex = mCb ? mCb(unitId, addr, cnt, mRegs) : MB_EX_GW_TARGET_FAILED;
            if(0 != ex) {
                sendException(c, req, ex);
                return;
            }

            uint16_t pduLen = 3 + 2 * cnt; // unit + fc + bytecount + data
            mTx[0] = req[0]; mTx[1] = req[1];  // transaction id
            mTx[2] = 0;      mTx[3] = 0;       // protocol id
            mTx[4] = pduLen >> 8;
            mTx[5] = pduLen & 0xff;
            mTx[6] = unitId;
            mTx[7] = fc;
            mTx[8] = 2 * cnt;
            for(uint16_t i = 0; i < cnt; i++) {
                mTx[9 + 2*i]     = mRegs[i] >> 8;
                mTx[9 + 2*i + 1] = mRegs[i] & 0xff;
            }
            send(c, mTx, 6 + pduLen);
        }

        void sendException(AsyncClient *c, const uint8_t *req, uint8_t code) {
            uint8_t tx[9];
            tx[0] = req[0]; tx[1] = req[1];
            tx[2] = 0;      tx[3] = 0;
            tx[4] = 0;      tx[5] = 3;
            tx[6] = req[6];
            tx[7] = req[7] | 0x80;
            tx[8] = code;
            send(c, tx, 9);
        }

        void send(AsyncClient *c, const uint8_t *data, size_t len) {
            if(c->space() >= len) {
                c->add(reinterpret_cast<const char*>(data), len);
                c->send();
            }
        }

        bool isAllowed(IPAddress remote) {
            if(0 == mAllowCnt)
                return true;
            uint32_t ip = toU32(remote);
            for(uint8_t i = 0; i < mAllowCnt; i++) {
                if((ip & mAllow[i].mask) == (mAllow[i].ip & mAllow[i].mask))
                    return true;
            }
            return false;
        }

        static uint32_t toU32(IPAddress a) {
            return ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | (uint32_t)a[3];
        }

        void parseAllowList(const char *list) {
            mAllowCnt = 0;
            if(nullptr == list)
                return;

            char entry[24];
            const char *p = list;
            while((*p != '\0') && (mAllowCnt < MODBUS_MAX_ALLOW_ENTRIES)) {
                // skip separators
                while((*p == ',') || (*p == ' ') || (*p == ';'))
                    p++;
                if(*p == '\0')
                    break;
                uint8_t n = 0;
                while((*p != '\0') && (*p != ',') && (*p != ' ') && (*p != ';')) {
                    if(n < (sizeof(entry) - 1))
                        entry[n++] = *p;
                    p++;
                }
                entry[n] = '\0';

                uint8_t prefix = 32;
                char *slash = strchr(entry, '/');
                if(nullptr != slash) {
                    *slash = '\0';
                    int pre = atoi(slash + 1);
                    if((pre < 0) || (pre > 32))
                        continue;
                    prefix = (uint8_t)pre;
                }

                IPAddress ip;
                if(!ip.fromString(entry)) {
                    DPRINT(DBG_WARN, F("Modbus: invalid allow entry "));
                    DBGPRINTLN(String(entry));
                    continue;
                }
                mAllow[mAllowCnt].ip   = toU32(ip);
                mAllow[mAllowCnt].mask = (0 == prefix) ? 0 : (0xffffffffUL << (32 - prefix));
                mAllowCnt++;
            }
        }

    private:
        AsyncServer *mServer = nullptr;
        readCb mCb;
        uint8_t mMaxClients = 2;
        volatile uint8_t mNumClients = 0;
        uint32_t mReqCnt = 0;
        uint32_t mRejectCnt = 0;

        AllowEntry mAllow[MODBUS_MAX_ALLOW_ENTRIES];
        uint8_t mAllowCnt = 0;

        // all AsyncTCP callbacks run in one task (ESP32) or in the
        // system context (ESP8266), therefore one shared buffer is enough
        uint16_t mRegs[MODBUS_MAX_READ_REGS];
        uint8_t mTx[9 + 2 * MODBUS_MAX_READ_REGS];
};

#endif /*ENABLE_SUNSPEC*/
#endif /*__MODBUS_TCP_SERVER_H__*/
