#pragma once
#include "hmDefines.h"
#include "../config/settings.h"
#include <map>
#include <utility>
template<class T=float> struct record_t { uint32_t ts = 0; };
template<class T=float>
class Inverter {
  public:
    uint8_t id = 0, ivGen = IV_HM, channels = 1; uint16_t actPowerLimit = 0xffff, maxPwr = 0, fw = 0;
    cfgIv_t *config = nullptr; record_t<> meas, info;
    std::map<std::pair<int,int>, float> v;
    record_t<> *getRecordStruct(uint8_t cmd) { return (cmd == RealTimeRunData_Debug) ? &meas : &info; }
    float getChannelFieldValue(uint8_t ch, uint8_t fld, record_t<> *rec) { if(rec != &meas) return 0; auto it = v.find({ch, fld}); return (it == v.end()) ? 0 : it->second; }
    uint16_t getMaxPower() { return maxPwr; }
    uint16_t getFwVersion() { return fw; }
};
template<uint8_t N>
class HmSystem {
  public:
    Inverter<> iv[N];
    Inverter<> *getInverterByPos(uint8_t p) { if(p >= N || nullptr == iv[p].config || 0 == iv[p].config->serial.u64) return nullptr; return &iv[p]; }
};
