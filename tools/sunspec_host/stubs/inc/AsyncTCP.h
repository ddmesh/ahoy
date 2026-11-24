#pragma once
// POSIX single threaded shim of the AsyncTCP API used by ModbusTcpServer.h
#include <Arduino.h>
#include <functional>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <poll.h>
class AsyncClient;
typedef std::function<void(void*, AsyncClient*)> AcConnectHandler;
typedef std::function<void(void*, AsyncClient*, void*, size_t)> AcDataHandler;
typedef std::function<void(void*, AsyncClient*, uint32_t)> AcTimeoutHandler;
class AsyncClient {
  public:
    int fd; IPAddress ip; bool closed = false;
    AcDataHandler dataCb; void *dataArg = nullptr;
    AcConnectHandler discCb; void *discArg = nullptr;
    std::string out;
    void setRxTimeout(uint32_t) {}
    IPAddress remoteIP() { return ip; }
    void onData(AcDataHandler cb, void *arg) { dataCb = cb; dataArg = arg; }
    void onDisconnect(AcConnectHandler cb, void *arg) { discCb = cb; discArg = arg; }
    void onTimeout(AcTimeoutHandler, void*) {}
    size_t space() { return 5744; }
    size_t add(const char *d, size_t l) { out.append(d, l); return l; }
    bool send() { ::send(fd, out.data(), out.size(), 0); out.clear(); return true; }
    void close(bool) { closed = true; }
};
class AsyncServer {
  public:
    uint16_t port; int lfd = -1; AcConnectHandler cb; std::vector<AsyncClient*> clients;
    static AsyncServer *inst;
    AsyncServer(uint16_t p) : port(p) { inst = this; }
    void onClient(AcConnectHandler c, void*) { cb = c; }
    void setNoDelay(bool) {}
    void begin() {
        lfd = socket(AF_INET, SOCK_STREAM, 0); int one = 1; setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if(bind(lfd, (sockaddr*)&a, sizeof(a)) || listen(lfd, 4)) { perror("bind"); exit(1); }
    }
    void end() {}
    void poll1(int ms) {
        std::vector<pollfd> p; p.push_back({lfd, POLLIN, 0});
        for(auto c : clients) p.push_back({c->fd, POLLIN, 0});
        if(::poll(p.data(), p.size(), ms) <= 0) return;
        if(p[0].revents & POLLIN) {
            sockaddr_in a{}; socklen_t l = sizeof(a); int fd = accept(lfd, (sockaddr*)&a, &l);
            AsyncClient *c = new AsyncClient; c->fd = fd; uint32_t ip = ntohl(a.sin_addr.s_addr);
            c->ip = IPAddress(ip >> 24, ip >> 16, ip >> 8, ip);
            clients.push_back(c); cb(nullptr, c);
        }
        for(size_t i = 1; i < p.size(); i++) {
            AsyncClient *c = clients[i-1];
            if(!c->closed && (p[i].revents & (POLLIN|POLLHUP))) {
                char buf[512]; ssize_t n = recv(c->fd, buf, sizeof(buf), 0);
                if(n <= 0) c->closed = true; else if(c->dataCb) c->dataCb(c->dataArg, c, buf, n);
            }
        }
        for(size_t i = 0; i < clients.size();) {
            AsyncClient *c = clients[i];
            if(c->closed) { ::close(c->fd); clients.erase(clients.begin() + i); if(c->discCb) c->discCb(c->discArg, c); else delete c; }
            else i++;
        }
    }
};
AsyncServer *AsyncServer::inst = nullptr;
