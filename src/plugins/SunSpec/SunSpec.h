//-----------------------------------------------------------------------------
// 2024 Ahoy, https://ahoydtu.de
// Creative Commons - https://creativecommons.org/licenses/by-nc-sa/4.0/deed
//-----------------------------------------------------------------------------
// SunSpec (read-only) over Modbus TCP
//
// Combined mode: one SunSpec device (unit id configurable, default 126)
//                which holds the sum of all SunSpec enabled inverters
// Separate mode: one SunSpec device per SunSpec enabled inverter,
//                unit id = inverter slot + 1 (1 ... MAX_NUM_INVERTERS)
//
// Register map of each device starts at 40000:
//   "SunS", Model 1 (common), Model 101 / 103 (inverter), end model
//   optional (compile switch in config.h): Model 120 (nameplate,
//   SUNSPEC_MODEL_120), Model 160 (MPPT, separate mode only, SUNSPEC_MODEL_160)
//
// Phases: Hoymiles single phase inverters do not report the grid phase they
// are connected to. It is configured per inverter (cfgIv_t::ssPhase) and used
// in combined mode: if the included inverters feed more than one phase (or an
// HMT is included) the combined device is a three phase device (Model 103)
// with per phase current and voltage.
//
// See doc/sunspec.md for details. Only compiled with ENABLE_SUNSPEC.
//-----------------------------------------------------------------------------

#ifndef __SUNSPEC_H__
#define __SUNSPEC_H__

#include "../../config/config.h" // ENABLE_SUNSPEC switch

#if defined(ENABLE_SUNSPEC)

#include <Arduino.h>
#include "../../appInterface.h"
#include "../../config/settings.h"
#include "../../hm/hmSystem.h"
#include "ModbusTcpServer.h"

#define SUNSPEC_BASE_ADDR       40000
// map size: header 2 + model 1 (68) + model 101/103 (52) + end 2
#if defined(SUNSPEC_MODEL_120)
    #define SUNSPEC_REGS_120    28
#else
    #define SUNSPEC_REGS_120    0
#endif
#if defined(SUNSPEC_MODEL_160)
    #define SUNSPEC_REGS_160    130     // 2 + 8 + 6 trackers * 20
#else
    #define SUNSPEC_REGS_160    0
#endif
#define SUNSPEC_MAX_REGS        (124 + SUNSPEC_REGS_120 + SUNSPEC_REGS_160)

#define SUNSPEC_MODE_COMBINED   0
#define SUNSPEC_MODE_SEPARATE   1

// not implemented values according to SunSpec
#define SS_NI_INT16             0x8000
#define SS_NI_UINT16            0xFFFF
#define SS_NI_SF                0x8000

// SunSpec operating states (model 101 / 103 'St')
#define SS_ST_OFF               1
#define SS_ST_SLEEPING          2
#define SS_ST_STARTING          3
#define SS_ST_MPPT              4
#define SS_ST_THROTTLED         5
#define SS_ST_STANDBY           8

template<class HMSYSTEM>
class SunSpec {
    public:
        SunSpec() {}

        void setup(IApp *app, HMSYSTEM *sys, settings_t *config) {
            mApp    = app;
            mSys    = sys;
            mConfig = config;
            mCfg    = &config->plugin.sunspec;
        }

        // start server, call once network is available
        void begin(void) {
            if(!mCfg->enabled || mStarted)
                return;
            mStarted = true;
            mServer.begin(mCfg->port, mCfg->maxClients, mCfg->allow,
                [this](uint8_t unitId, uint16_t addr, uint16_t cnt, uint16_t *dst) -> uint8_t {
                    return onRead(unitId, addr, cnt, dst);
                });
        }

        bool isStarted(void) const {
            return mStarted;
        }

        uint8_t getNumClients(void) const {
            return mServer.getNumClients();
        }

        uint32_t getRequestCnt(void) const {
            return mServer.getRequestCnt();
        }

        uint32_t getRejectCnt(void) const {
            return mServer.getRejectCnt();
        }

    private:
        //---------------------------------------------------------------------
        // Modbus read callback
        //---------------------------------------------------------------------
        uint8_t onRead(uint8_t unitId, uint16_t addr, uint16_t cnt, uint16_t *dst) {
            uint16_t len = 0;

            if(SUNSPEC_MODE_SEPARATE == mCfg->mode) {
                if((0 == unitId) || (unitId > MAX_NUM_INVERTERS))
                    return MB_EX_GW_TARGET_FAILED;
                Inverter<> *iv = mSys->getInverterByPos(unitId - 1);
                if(!isIncluded(iv))
                    return MB_EX_GW_TARGET_FAILED;
                len = buildInverterMap(iv);
            } else {
                if(unitId != mCfg->unitId)
                    return MB_EX_GW_TARGET_FAILED;
                len = buildTotalMap();
            }

            if((addr < SUNSPEC_BASE_ADDR) || ((uint32_t)(addr - SUNSPEC_BASE_ADDR) + cnt > len))
                return MB_EX_ILLEGAL_ADDRESS;

            memcpy(dst, &mMap[addr - SUNSPEC_BASE_ADDR], cnt * sizeof(uint16_t));
            return 0;
        }

        bool isIncluded(Inverter<> *iv) {
            if(nullptr == iv)
                return false;
            if(nullptr == iv->config)
                return false;
            return (iv->config->enabled && iv->config->sunspec);
        }

        // does not modify inverter state (called from TCP context)
        bool isAvailable(Inverter<> *iv) {
            record_t<> *rec = iv->getRecordStruct(RealTimeRunData_Debug);
            if(0 == rec->ts)
                return false;
            return ((mApp->getTimestamp() - rec->ts) < INVERTER_INACT_THRES_SEC);
        }

        uint16_t ivState(Inverter<> *iv) {
            if(!isAvailable(iv))
                return SS_ST_SLEEPING;
            record_t<> *rec = iv->getRecordStruct(RealTimeRunData_Debug);
            if(iv->getChannelFieldValue(CH0, FLD_PAC, rec) > INACT_PWR_THRESH) {
                if(iv->actPowerLimit < 100)
                    return SS_ST_THROTTLED;
                return SS_ST_MPPT;
            }
            return SS_ST_STANDBY;
        }

        //---------------------------------------------------------------------
        // register map helpers
        //---------------------------------------------------------------------
        void putStr(uint16_t pos, uint16_t regs, const char *str) {
            uint16_t n = (nullptr == str) ? 0 : strlen(str);
            for(uint16_t i = 0; i < regs; i++) {
                uint8_t hi = ((2*i)     < n) ? str[2*i]     : 0;
                uint8_t lo = ((2*i + 1) < n) ? str[2*i + 1] : 0;
                mMap[pos + i] = (hi << 8) | lo;
            }
        }

        void put32(uint16_t pos, uint32_t val) {
            mMap[pos]     = val >> 16;
            mMap[pos + 1] = val & 0xffff;
        }

        void fill(uint16_t pos, uint16_t regs, uint16_t val) {
            for(uint16_t i = 0; i < regs; i++)
                mMap[pos + i] = val;
        }

        // returns the smallest scale factor >= minSf which lets all values fit
        // into a signed 16 bit integer
        static int16_t findSf(float maxAbs, int16_t minSf) {
            int16_t sf = minSf;
            while((sf < 10) && ((maxAbs / ssPow10(sf)) > 32767.0f))
                sf++;
            return sf;
        }

        static float ssPow10(int16_t sf) {
            float f = 1.0f;
            if(sf >= 0) {
                for(int16_t i = 0; i < sf; i++)
                    f *= 10.0f;
            } else {
                for(int16_t i = 0; i > sf; i--)
                    f /= 10.0f;
            }
            return f;
        }

        static uint16_t scaled(float val, int16_t sf) {
            float v = val / ssPow10(sf);
            v += (v >= 0) ? 0.5f : -0.5f;
            if(v > 32767.0f)  v = 32767.0f;
            if(v < -32767.0f) v = -32767.0f;
            return (uint16_t)(int16_t)v;
        }

        static float absf(float v) {
            return (v < 0) ? -v : v;
        }

        // --------------------------------------------------------------------
        uint16_t putHeader(void) {
            putStr(0, 2, "SunS");
            return 2;
        }

        // Model 1: common block, 66 registers
        uint16_t putCommon(uint16_t pos, const char *mn, const char *md, const char *opt, const char *vr, const char *sn, uint16_t devAddr) {
            mMap[pos++] = 1;
            mMap[pos++] = 66;
            putStr(pos,      16, mn);   // Mn
            putStr(pos + 16, 16, md);   // Md
            putStr(pos + 32,  8, opt);  // Opt
            putStr(pos + 40,  8, vr);   // Vr
            putStr(pos + 48, 16, sn);   // SN
            mMap[pos + 64] = devAddr;   // DA
            mMap[pos + 65] = 0x8000;    // Pad
            return pos + 66;
        }

        struct acData_t {
            float a[4];         // total, L1, L2, L3
            float v[3];         // L1-N, L2-N, L3-N
            float w;
            float hz;
            float var;
            float pf;           // 0 ... 1
            float wh;
            float dcw;
            float tmp;
            uint16_t st;
            bool threePhase;
            bool hasTmp;
            bool hasHz;
            bool hasPf;
        };

        // Model 101 (1 phase) or 103 (3 phase), int + scale factor, 50 registers
        uint16_t putInverter(uint16_t pos, acData_t *d) {
            mMap[pos++] = d->threePhase ? 103 : 101;
            mMap[pos++] = 50;
            uint16_t p = pos;
            fill(p, 50, SS_NI_INT16);

            // current
            float maxA = absf(d->a[0]);
            int16_t aSf = findSf(maxA, -2);
            mMap[p + 0] = scaled(d->a[0], aSf);
            if(d->threePhase) {
                mMap[p + 1] = scaled(d->a[1], aSf);
                mMap[p + 2] = scaled(d->a[2], aSf);
                mMap[p + 3] = scaled(d->a[3], aSf);
            } else
                mMap[p + 1] = scaled(d->a[0], aSf);
            mMap[p + 4] = (uint16_t)aSf;

            // voltage (phase to phase not available)
            mMap[p + 5] = SS_NI_UINT16;
            mMap[p + 6] = SS_NI_UINT16;
            mMap[p + 7] = SS_NI_UINT16;
            mMap[p + 8] = scaled(d->v[0], -1);
            if(d->threePhase) {
                mMap[p + 9]  = scaled(d->v[1], -1);
                mMap[p + 10] = scaled(d->v[2], -1);
            } else {
                mMap[p + 9]  = SS_NI_UINT16;
                mMap[p + 10] = SS_NI_UINT16;
            }
            mMap[p + 11] = (uint16_t)(int16_t)-1;

            // power
            int16_t wSf = findSf(absf(d->w), -1);
            mMap[p + 12] = scaled(d->w, wSf);
            mMap[p + 13] = (uint16_t)wSf;

            // frequency
            if(d->hasHz) {
                mMap[p + 14] = scaled(d->hz, -2);
                mMap[p + 15] = (uint16_t)(int16_t)-2;
            } else {
                mMap[p + 14] = SS_NI_UINT16;
                mMap[p + 15] = SS_NI_SF;
            }

            // VA not available
            mMap[p + 16] = SS_NI_INT16;
            mMap[p + 17] = SS_NI_SF;

            // VAr
            int16_t varSf = findSf(absf(d->var), -1);
            mMap[p + 18] = scaled(d->var, varSf);
            mMap[p + 19] = (uint16_t)varSf;

            // PF in percent
            if(d->hasPf) {
                mMap[p + 20] = scaled(d->pf * 100.0f, -1);
                mMap[p + 21] = (uint16_t)(int16_t)-1;
            } else
                mMap[p + 21] = SS_NI_SF;

            // energy, acc32, Wh
            put32(p + 22, (uint32_t)(d->wh + 0.5f));
            mMap[p + 24] = 0;

            // DC: only power is meaningful for multi input inverters
            mMap[p + 25] = SS_NI_UINT16;
            mMap[p + 26] = SS_NI_SF;
            mMap[p + 27] = SS_NI_UINT16;
            mMap[p + 28] = SS_NI_SF;
            int16_t dcwSf = findSf(absf(d->dcw), -1);
            mMap[p + 29] = scaled(d->dcw, dcwSf);
            mMap[p + 30] = (uint16_t)dcwSf;

            // temperature
            if(d->hasTmp) {
                mMap[p + 31] = scaled(d->tmp, -1);
                mMap[p + 35] = (uint16_t)(int16_t)-1;
            } else
                mMap[p + 35] = SS_NI_SF;
            // p + 32 ... 34: not implemented

            // state
            mMap[p + 36] = d->st;
            mMap[p + 37] = SS_NI_UINT16;
            fill(p + 38, 12, 0); // events: none

            return pos + 50;
        }

        #if defined(SUNSPEC_MODEL_120)
        // Model 120: nameplate, 26 registers
        uint16_t putNameplate(uint16_t pos, float wRtg) {
            mMap[pos++] = 120;
            mMap[pos++] = 26;
            fill(pos, 26, SS_NI_UINT16);
            mMap[pos + 0] = 4; // DERTyp: PV
            if(wRtg > 0) {
                int16_t sf = findSf(wRtg, 0);
                mMap[pos + 1] = scaled(wRtg, sf); // WRtg
                mMap[pos + 2] = (uint16_t)sf;
                mMap[pos + 3] = scaled(wRtg, sf); // VARtg
                mMap[pos + 4] = (uint16_t)sf;
            } else {
                mMap[pos + 2] = SS_NI_SF;
                mMap[pos + 4] = SS_NI_SF;
            }
            // VArRtgQ1..4 (int16), PFRtgQ1..4 (int16): not implemented
            for(uint8_t i = 5; i <= 8; i++)
                mMap[pos + i] = SS_NI_INT16;
            for(uint8_t i = 12; i <= 15; i++)
                mMap[pos + i] = SS_NI_INT16;
            mMap[pos + 9]  = SS_NI_SF;
            mMap[pos + 11] = SS_NI_SF;
            mMap[pos + 16] = SS_NI_SF;
            mMap[pos + 18] = SS_NI_SF;
            mMap[pos + 20] = SS_NI_SF;
            mMap[pos + 22] = SS_NI_SF;
            mMap[pos + 24] = SS_NI_SF;
            mMap[pos + 25] = 0x8000; // Pad
            return pos + 26;
        }
        #endif /*SUNSPEC_MODEL_120*/

        #if defined(SUNSPEC_MODEL_160)
        // Model 160: multiple MPPT, 8 + 20 * N registers
        uint16_t putMppt(uint16_t pos, Inverter<> *iv) {
            uint8_t n = iv->channels;
            if(n > 6)
                n = 6;
            record_t<> *rec = iv->getRecordStruct(RealTimeRunData_Debug);
            bool avail = isAvailable(iv);

            float maxA = 0, maxV = 0, maxW = 0;
            for(uint8_t ch = 1; ch <= n; ch++) {
                float a = absf(iv->getChannelFieldValue(ch, FLD_IDC, rec));
                float v = absf(iv->getChannelFieldValue(ch, FLD_UDC, rec));
                float w = absf(iv->getChannelFieldValue(ch, FLD_PDC, rec));
                if(a > maxA) maxA = a;
                if(v > maxV) maxV = v;
                if(w > maxW) maxW = w;
            }
            int16_t aSf = findSf(maxA, -2);
            int16_t vSf = findSf(maxV, -1);
            int16_t wSf = findSf(maxW, -1);

            mMap[pos++] = 160;
            mMap[pos++] = 8 + 20 * n;
            mMap[pos + 0] = (uint16_t)aSf;
            mMap[pos + 1] = (uint16_t)vSf;
            mMap[pos + 2] = (uint16_t)wSf;
            mMap[pos + 3] = 0;          // DCWH_SF
            put32(pos + 4, 0);          // Evt
            mMap[pos + 6] = n;          // N
            mMap[pos + 7] = SS_NI_UINT16; // TmsPer
            pos += 8;

            for(uint8_t ch = 1; ch <= n; ch++) {
                char idStr[17];
                const char *chName = iv->config->chName[ch-1];
                if(chName[0] != '\0')
                    snprintf(idStr, sizeof(idStr), "%s", chName);
                else
                    snprintf(idStr, sizeof(idStr), "PV%d", ch);

                mMap[pos + 0] = ch;                     // ID
                putStr(pos + 1, 8, idStr);              // IDStr
                mMap[pos + 9]  = scaled(iv->getChannelFieldValue(ch, FLD_IDC, rec), aSf);
                mMap[pos + 10] = scaled(iv->getChannelFieldValue(ch, FLD_UDC, rec), vSf);
                mMap[pos + 11] = scaled(iv->getChannelFieldValue(ch, FLD_PDC, rec), wSf);
                put32(pos + 12, (uint32_t)(iv->getChannelFieldValue(ch, FLD_YT, rec) * 1000.0f + 0.5f));
                put32(pos + 14, 0xffffffff);            // Tms: not implemented
                mMap[pos + 16] = SS_NI_INT16;           // Tmp
                mMap[pos + 17] = avail ? SS_ST_MPPT : SS_ST_SLEEPING; // DCSt
                put32(pos + 18, 0);                     // DCEvt
                pos += 20;
            }
            return pos;
        }
        #endif /*SUNSPEC_MODEL_160*/

        uint16_t putEnd(uint16_t pos) {
            mMap[pos++] = 0xFFFF;
            mMap[pos++] = 0;
            return pos;
        }

        //---------------------------------------------------------------------
        // device maps
        //---------------------------------------------------------------------
        void getModelName(Inverter<> *iv, char *buf, uint8_t len) {
            const char *pre;
            switch(iv->ivGen) {
                case IV_MI:  pre = "MI-";  break;
                case IV_HM:  pre = "HM-";  break;
                case IV_HMS: pre = "HMS-"; break;
                case IV_HMT: pre = "HMT-"; break;
                default:     pre = "";     break;
            }
            uint16_t maxPwr = iv->getMaxPower();
            if(0 == maxPwr)
                snprintf(buf, len, "%sInverter", pre);
            else
                snprintf(buf, len, "%s%d", pre, maxPwr);
        }

        void readAc(Inverter<> *iv, acData_t *d) {
            record_t<> *rec = iv->getRecordStruct(RealTimeRunData_Debug);
            bool avail = isAvailable(iv);

            memset(d, 0, sizeof(acData_t));
            d->threePhase = (IV_HMT == iv->ivGen);
            d->hasTmp     = avail;
            d->hasHz      = avail;
            d->hasPf      = avail;
            d->wh         = iv->getChannelFieldValue(CH0, FLD_YT, rec) * 1000.0f;
            d->st         = ivState(iv);
            if(!avail)
                return;

            d->w    = iv->getChannelFieldValue(CH0, FLD_PAC, rec);
            d->hz   = iv->getChannelFieldValue(CH0, FLD_F, rec);
            d->var  = iv->getChannelFieldValue(CH0, FLD_Q, rec);
            d->pf   = iv->getChannelFieldValue(CH0, FLD_PF, rec);
            d->dcw  = iv->getChannelFieldValue(CH0, FLD_PDC, rec);
            d->tmp  = iv->getChannelFieldValue(CH0, FLD_T, rec);
            if(d->threePhase) {
                d->a[1] = iv->getChannelFieldValue(CH0, FLD_IAC_1, rec);
                d->a[2] = iv->getChannelFieldValue(CH0, FLD_IAC_2, rec);
                d->a[3] = iv->getChannelFieldValue(CH0, FLD_IAC_3, rec);
                d->a[0] = d->a[1] + d->a[2] + d->a[3];
                d->v[0] = iv->getChannelFieldValue(CH0, FLD_UAC_1N, rec);
                d->v[1] = iv->getChannelFieldValue(CH0, FLD_UAC_2N, rec);
                d->v[2] = iv->getChannelFieldValue(CH0, FLD_UAC_3N, rec);
            } else {
                d->a[0] = iv->getChannelFieldValue(CH0, FLD_IAC, rec);
                d->v[0] = iv->getChannelFieldValue(CH0, FLD_UAC, rec);
            }
        }

        uint16_t buildInverterMap(Inverter<> *iv) {
            char model[24], sn[17], vr[17];
            getModelName(iv, model, sizeof(model));
            snprintf(sn, sizeof(sn), "%012llX", (unsigned long long)iv->config->serial.u64);
            uint16_t fw = iv->getFwVersion();
            snprintf(vr, sizeof(vr), "%d.%02d.%02d", (fw / 10000), ((fw % 10000) / 100), (fw % 100));

            acData_t d;
            readAc(iv, &d);

            uint16_t pos = putHeader();
            pos = putCommon(pos, "Hoymiles", model, iv->config->name, vr, sn, iv->id + 1);
            pos = putInverter(pos, &d);
            #if defined(SUNSPEC_MODEL_120)
            pos = putNameplate(pos, (float)iv->getMaxPower());
            #endif
            #if defined(SUNSPEC_MODEL_160)
            pos = putMppt(pos, iv);
            #endif
            return putEnd(pos);
        }

        // grid phase of a single phase inverter (0: L1, 1: L2, 2: L3)
        static uint8_t ivPhase(Inverter<> *iv) {
            return (iv->config->ssPhase > 2) ? 0 : iv->config->ssPhase;
        }

        uint16_t buildTotalMap(void) {
            acData_t t;
            memset(&t, 0, sizeof(acData_t));
            t.st = SS_ST_SLEEPING;
            float vSum[3] = {0, 0, 0}, hzSum = 0;
            uint8_t vCnt[3] = {0, 0, 0};
            uint8_t nAvail = 0, phMask = 0;
            bool throttled = false, producing = false, standby = false;
            #if defined(SUNSPEC_MODEL_120)
            float wRtg = 0;
            #endif

            for(uint8_t i = 0; i < MAX_NUM_INVERTERS; i++) {
                Inverter<> *iv = mSys->getInverterByPos(i);
                if(!isIncluded(iv))
                    continue;

                acData_t d;
                readAc(iv, &d);
                t.wh += d.wh;
                #if defined(SUNSPEC_MODEL_120)
                wRtg += iv->getMaxPower();
                #endif
                // phases fed by the included inverters (from the settings,
                // not from the availability, so the model id stays stable)
                uint8_t ph = ivPhase(iv);
                phMask |= d.threePhase ? 0x07 : (1 << ph);

                switch(d.st) {
                    case SS_ST_THROTTLED: throttled = true; break;
                    case SS_ST_MPPT:      producing = true; break;
                    case SS_ST_STANDBY:   standby   = true; break;
                    default: break;
                }
                if(SS_ST_SLEEPING == d.st)
                    continue;

                nAvail++;
                t.a[0] += d.a[0];
                t.w    += d.w;
                t.var  += d.var;
                t.dcw  += d.dcw;
                hzSum  += d.hz;
                if(d.tmp > t.tmp)
                    t.tmp = d.tmp;
                if(d.threePhase) {
                    for(uint8_t p = 0; p < 3; p++) {
                        t.a[1 + p] += d.a[1 + p];
                        vSum[p]    += d.v[p];
                        vCnt[p]++;
                    }
                } else {
                    t.a[1 + ph] += d.a[0];
                    vSum[ph]    += d.v[0];
                    vCnt[ph]++;
                }
            }

            if(nAvail > 0) {
                t.hz     = hzSum / nAvail;
                t.hasHz  = true;
                t.hasTmp = true;
            }

            // more than one phase fed: three phase device (Model 103), a phase
            // without producing inverter reports 0 A and 0 V (the Cerbo GX
            // splits W by V * I per phase, so the value must be valid)
            t.threePhase = (0 != (phMask & (phMask - 1)));
            if(t.threePhase) {
                for(uint8_t p = 0; p < 3; p++)
                    t.v[p] = (vCnt[p] > 0) ? (vSum[p] / vCnt[p]) : 0;
            } else {
                for(uint8_t p = 0; p < 3; p++) {
                    if(phMask & (1 << p))
                        t.v[0] = (vCnt[p] > 0) ? (vSum[p] / vCnt[p]) : 0;
                }
            }

            if(throttled)
                t.st = SS_ST_THROTTLED;
            else if(producing)
                t.st = SS_ST_MPPT;
            else if(standby)
                t.st = SS_ST_STANDBY;

            char sn[17], md[33];
            String mac = mApp->getMac();
            mac.replace(":", "");
            snprintf(sn, sizeof(sn), "AHOY%s", mac.c_str());
            snprintf(md, sizeof(md), "%s", mConfig->sys.deviceName);

            uint16_t pos = putHeader();
            pos = putCommon(pos, "AhoyDTU", md, "total", mApp->getVersion(), sn, mCfg->unitId);
            pos = putInverter(pos, &t);
            #if defined(SUNSPEC_MODEL_120)
            pos = putNameplate(pos, wRtg);
            #endif
            return putEnd(pos);
        }

    private:
        IApp *mApp = nullptr;
        HMSYSTEM *mSys = nullptr;
        settings_t *mConfig = nullptr;
        cfgSunSpec_t *mCfg = nullptr;
        bool mStarted = false;

        ModbusTcpServer mServer;
        // built on each request, all AsyncTCP callbacks are serialized
        uint16_t mMap[SUNSPEC_MAX_REGS];
};

#endif /*ENABLE_SUNSPEC*/
#endif /*__SUNSPEC_H__*/
