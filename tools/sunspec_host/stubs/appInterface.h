#pragma once
#include <Arduino.h>
class IApp {
  public:
    uint32_t ts = 1000000;
    uint32_t getTimestamp() { return ts; }
    String getMac() { return String("A0:B1:C2:D3:E4:F5"); }
    const char *getVersion() { return "0.8.156"; }
};
