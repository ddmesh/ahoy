#include "plugins/SunSpec/SunSpec.h"
#include <csignal>
#include <cstring>
typedef HmSystem<MAX_NUM_INVERTERS> Sys;
int main(int argc, char **argv) {
    static settings_t cfg{}; static Sys sys; static IApp app;
    strcpy(cfg.sys.deviceName, "AHOY-DTU");
    cfg.plugin.sunspec = {true, (uint16_t)atoi(argv[1]), (uint8_t)atoi(argv[2]), 126, 2, ""};
    if(argc > 3) snprintf(cfg.plugin.sunspec.allow, SUNSPEC_ALLOW_LEN, "%s", argv[3]);
    // iv0: HM-800 producing, 2 channels; iv1: HMT-2250 3 phase; iv2: sunspec off; iv3: unavailable
    const char *names[] = {"Garage", "Dach", "Balkon", "Schuppen"};
    uint64_t sn[] = {0x116181234567ULL, 0x138281234568ULL, 0x114181234569ULL, 0x112181234570ULL};
    for(int i = 0; i < 4; i++) {
        cfg.iv[i].enabled = true; cfg.iv[i].sunspec = (i != 2); cfg.iv[i].serial.u64 = sn[i];
        snprintf(cfg.iv[i].name, MAX_NAME_LENGTH, "%s", names[i]);
        sys.iv[i].config = &cfg.iv[i]; sys.iv[i].id = i; sys.iv[i].meas.ts = app.ts - 10;
    }
    auto &a = sys.iv[0]; a.ivGen = IV_HM; a.channels = 2; a.maxPwr = 800; a.fw = 10018;
    a.v[{0,FLD_PAC}] = 612.3f; a.v[{0,FLD_UAC}] = 231.4f; a.v[{0,FLD_IAC}] = 2.65f; a.v[{0,FLD_F}] = 50.01f; a.v[{0,FLD_PF}] = 1.0f;
    a.v[{0,FLD_YT}] = 1234.567f; a.v[{0,FLD_PDC}] = 640.0f; a.v[{0,FLD_T}] = 38.5f;
    a.v[{1,FLD_UDC}] = 35.2f; a.v[{1,FLD_IDC}] = 9.1f; a.v[{1,FLD_PDC}] = 320.3f; a.v[{1,FLD_YT}] = 600.1f;
    a.v[{2,FLD_UDC}] = 34.8f; a.v[{2,FLD_IDC}] = 9.2f; a.v[{2,FLD_PDC}] = 319.7f; a.v[{2,FLD_YT}] = 634.4f;
    snprintf(cfg.iv[0].chName[0], MAX_NAME_LENGTH, "Ost");
    auto &b = sys.iv[1]; b.ivGen = IV_HMT; b.channels = 6; b.maxPwr = 2250; b.fw = 10106; b.actPowerLimit = 80;
    b.v[{0,FLD_PAC}] = 1800.0f; b.v[{0,FLD_UAC_1N}] = 230.0f; b.v[{0,FLD_UAC_2N}] = 231.0f; b.v[{0,FLD_UAC_3N}] = 232.0f;
    b.v[{0,FLD_IAC_1}] = 2.6f; b.v[{0,FLD_IAC_2}] = 2.6f; b.v[{0,FLD_IAC_3}] = 2.6f; b.v[{0,FLD_F}] = 49.99f; b.v[{0,FLD_YT}] = 5000.0f;
    auto &c = sys.iv[2]; c.maxPwr = 400; c.v[{0,FLD_PAC}] = 300.0f; c.v[{0,FLD_YT}] = 100.0f;
    auto &d = sys.iv[3]; d.maxPwr = 600; d.meas.ts = app.ts - 3600; d.v[{0,FLD_YT}] = 777.0f;
    // phase scenario (argv[4]): "1ph": HMT excluded, all on L1 -> model 101
    // "2ph": HMT excluded, iv3 producing on L2 -> model 103 (L3 = 0 A / 0 V)
    if(argc > 4) {
        cfg.iv[1].sunspec = false;
        if(0 == strcmp(argv[4], "2ph")) {
            cfg.iv[3].ssPhase = 1; d.ivGen = IV_HMS; d.meas.ts = app.ts - 10;
            d.v[{0,FLD_PAC}] = 300.0f; d.v[{0,FLD_UAC}] = 233.0f; d.v[{0,FLD_IAC}] = 1.3f; d.v[{0,FLD_F}] = 50.0f;
        }
    }
    SunSpec<Sys> ss; ss.setup(&app, &sys, &cfg); ss.begin();
    fflush(stdout);
    for(;;) { AsyncServer::inst->poll1(100); fflush(stdout); }
}
