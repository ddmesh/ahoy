#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cmath>
#define F(x) x
class String {
  public:
    std::string s;
    String(const char *c = "") : s(c) {}
    String(std::string c) : s(c) {}
    String(unsigned v) : s(std::to_string(v)) {}
    const char *c_str() const { return s.c_str(); }
    void replace(const char *a, const char *b) { size_t p; while((p = s.find(a)) != std::string::npos) s.replace(p, strlen(a), b); }
};
class IPAddress {
  public:
    uint8_t b[4] = {0,0,0,0};
    IPAddress() {}
    IPAddress(uint8_t a,uint8_t c,uint8_t d,uint8_t e){b[0]=a;b[1]=c;b[2]=d;b[3]=e;}
    uint8_t operator[](int i) const { return b[i]; }
    bool fromString(const char *s) { unsigned x[4]; char t; if(4 != sscanf(s, "%u.%u.%u.%u%c", &x[0],&x[1],&x[2],&x[3],&t)) return false; for(int i=0;i<4;i++){ if(x[i]>255) return false; b[i]=x[i];} return true; }
    String toString() const { char buf[20]; snprintf(buf, 20, "%u.%u.%u.%u", b[0],b[1],b[2],b[3]); return String(buf); }
};
