# SunSpec build results

Date: 2026-11-23. PlatformIO Core 6.2.0, `pio run` in `src/`. Branch `SunSpec`,
commit f601ab8 as the base. Flag on means `#define ENABLE_SUNSPEC` in
`src/config/config_override.h` (git-ignored, removed again afterwards).

| Environment | SunSpec | RAM (bytes) | Flash (bytes) | Result |
|---|---|---|---|---|
| esp8266-all | off | 55532 of 81920 (67.8%) | 637783 of 1044464 (61.1%) | success |
| esp8266-all | on | 56964 (69.5%) | 645987 (61.8%) | success |
| esp32-wroom32 | off | 81600 of 327680 (24.9%) | 1293237 of 1310720 (98.7%) | success |
| esp32-wroom32 | on | 82888 (25.3%) | 1301853 (99.3%) | success |

Growth with the flag on:

| Environment | RAM | Flash |
|---|---|---|
| esp8266-all | +1432 B | +8204 B |
| esp32-wroom32 | +1288 B | +8616 B |

The ESP8266 growth is inside the expected range of the plan (RAM < 2 kB,
flash < 15 kB). The ESP32 `esp32-wroom32` image uses 99.3% of the app
partition with the flag on. Only 8867 B are left, so little room for further
features in that environment.

Checks done:

- Both flag-on builds compile and link with no compiler error. No warning
  appears in the esp8266-all flag-on log (`grep -c warning` = 0). The ESP32
  logs were not checked for warnings.
- esp8266-all flag on: the linked ELF contains SunSpec / ModbusTcp symbols
  (44 matches) and the generated `web/html/tmp/setup.html` contains `ssEn`.
- `git diff` of the branch against `main` (src/ without `src/plugins/SunSpec`):
  8 files, 226 insertions, 0 deletions.

Not checked:

- No byte-for-byte comparison of flag-off firmware against `main`.
  The flag-off builds above are of the branch, not of `main`.
- Other environments (esp32-s2/c3, opendtufusion, esp8285, `-de`) were not built.
- Nothing ran on hardware (plan step 6, settings reload and heap checks).

## All environments (added later)

Sizes in bytes, from the `RAM:`/`Flash:` lines of `pio run`. All flag-off builds succeed.

| Environment | RAM off | RAM on | RAM delta | Flash off | Flash on | Flash delta | Flash limit | Build with flag on |
|---|---|---|---|---|---|---|---|---|
| esp32-c3-mini | 74916 | 76196 | +1280 | 1270266 | 1279524 | +9258 | 1310720 | success |
| esp32-c3-mini-de | 74916 | 76196 | +1280 | 1271458 | 1280772 | +9314 | 1310720 | success |
| esp32-s2-mini | 88064 | 89352 | +1288 | 1212718 | 1221490 | +8772 | 1310720 | success |
| esp32-s2-mini-de | 88064 | 89352 | +1288 | 1213946 | 1222758 | +8812 | 1310720 | success |
| esp32-wroom32 | 81600 | 82888 | +1288 | 1293237 | 1301853 | +8616 | 1310720 | success |
| esp32-wroom32-de | 81600 | 82888 | +1288 | 1294445 | 1303077 | +8632 | 1310720 | success |
| esp32-wroom32-minimal | 74304 | 75584 | +1280 | 1132901 | 1141805 | +8904 | 1310720 | success |
| esp32-wroom32-prometheus | 82096 | 83384 | +1288 | 1302069 | 1310829 | +8760 | 1310720 | **FAILED: flash overflow by 109 B** |
| esp32-wroom32-prometheus-de | 82096 | 83384 | +1288 | 1303277 | 1312053 | +8776 | 1310720 | **FAILED: flash overflow by 1333 B** |
| esp8266 | 50604 | 52020 | +1416 | 599303 | 607531 | +8228 | 1044464 | success |
| esp8266-all | 55532 | 56964 | +1432 | 637783 | 645987 | +8204 | 1044464 | success |
| esp8266-all-de | 55580 | 57012 | +1432 | 639011 | 647231 | +8220 | 1044464 | success |
| esp8266-de | 50636 | 52068 | +1432 | 600443 | 608711 | +8268 | 1044464 | success |
| esp8266-minimal | 46476 | 47904 | +1428 | 567387 | 575579 | +8192 | 1044464 | success |
| esp8266-prometheus | 57100 | 58536 | +1436 | 644827 | 652987 | +8160 | 1044464 | success |
| esp8266-prometheus-de | 57148 | 58584 | +1436 | 646055 | 654247 | +8192 | 1044464 | success |
| esp8285 | 55516 | 56948 | +1432 | 637695 | 645883 | +8188 | 958448 | success |
| esp8285-de | 55564 | 56996 | +1432 | 638923 | 647143 | +8220 | 958448 | success |
| opendtufusion | 103040 | 104328 | +1288 | 1248361 | 1256909 | +8548 | 3342336 | success |
| opendtufusion-16MB | 103040 | 104328 | +1288 | 1248377 | 1256925 | +8548 | 6553600 | success |
| opendtufusion-16MB-de | 103040 | 104328 | +1288 | 1249597 | 1258177 | +8580 | 3342336 | success |
| opendtufusion-de | 103040 | 104328 | +1288 | 1249581 | 1258161 | +8580 | 3342336 | success |
| opendtufusion-minimal | 95704 | 96984 | +1280 | 1089541 | 1098273 | +8732 | 3342336 | success |

Findings:

- `esp32-wroom32-prometheus` and `esp32-wroom32-prometheus-de` do **not fit**
  with SunSpec on: the image is 109 B and 1333 B over the 1310720 B app
  partition. They cannot use SunSpec unless something else is removed. The
  code was not changed for this.
- `esp32-wroom32` and `esp32-wroom32-de` fit, but with only 8867 B and 7643 B
  of flash left. `esp32-c3-mini` / `-de` are at 97.6% / 97.7%.
- Flag-on growth over all environments: RAM +1280 to +1436 B, flash +8160 to
  +9314 B.
- The flash limit shown for `opendtufusion-16MB-de` (3342336 B) is the same as
  for the non-16MB variants and differs from `opendtufusion-16MB` (6553600 B).
  I did not look into why; it is not caused by SunSpec (same value with the
  flag off).

## Minimal build and phase setting (2026-11-24)

Models 120 and 160 are now optional (`SUNSPEC_MODEL_120`, `SUNSPEC_MODEL_160`,
default off), the register buffer is sized to the enabled models, and a per
inverter grid phase setting was added (combined mode serves model 103 when the
included inverters feed more than one phase). Sizes in bytes from `pio run`,
all builds with `ENABLE_SUNSPEC` and without compiler warnings:

| Environment | Build | RAM | Flash | vs. before (RAM / flash) |
|---|---|---|---|---|
| esp8266-all | SunSpec off | 55532 | 637783 | |
| esp8266-all | before (120 + 160 always) | 56964 | 645987 | |
| esp8266-all | minimal (default) | 56612 | 645751 | -352 / -236 |
| esp8266-all | with 120 + 160 | 56940 | 646735 | -24 / +748 |
| esp32-wroom32 | before (120 + 160 always) | 82888 | 1301853 | |
| esp32-wroom32 | minimal (default) | 82536 | 1301381 | -352 / -472 |
| esp32-wroom32 | with 120 + 160 | 82848 | 1302421 | -40 / +568 |

Most of the remaining flag-on cost (ESP8266 minimal vs. off: +1080 B RAM,
+7968 B flash) is the Modbus TCP server, the settings and the web UI, not the
SunSpec models. The phase handling adds about 0.5 to 1 kB flash.

`tools/sunspec_host/run.sh` passes for both builds and the cases
HM + HMT (model 103), single phase on L1 only (model 101), HM on L1 + HMS on L2
(model 103, L3 = 0 A / 0 V) and separate mode. Not tested: real hardware,
Cerbo GX, web UI in a browser.
