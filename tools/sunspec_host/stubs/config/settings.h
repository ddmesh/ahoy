#pragma once
#include <Arduino.h>
#define ENABLE_SUNSPEC
#define MAX_NUM_INVERTERS 4
#define MAX_NAME_LENGTH 16
#define INVERTER_INACT_THRES_SEC 5*60
#define INACT_PWR_THRESH 0
union serial_u { uint64_t u64; uint8_t b[8]; };
typedef struct { bool enabled; char name[MAX_NAME_LENGTH]; serial_u serial; char chName[6][MAX_NAME_LENGTH]; bool sunspec; uint8_t ssPhase; } cfgIv_t;
#define SUNSPEC_ALLOW_LEN 80
typedef struct { bool enabled; uint16_t port; uint8_t mode; uint8_t unitId; uint8_t maxClients; char allow[SUNSPEC_ALLOW_LEN]; } cfgSunSpec_t;
typedef struct { cfgSunSpec_t sunspec; } plugins_t;
typedef struct { char deviceName[32]; } cfgSys_t;
typedef struct { cfgSys_t sys; plugins_t plugin; cfgIv_t iv[MAX_NUM_INVERTERS]; } settings_t;
