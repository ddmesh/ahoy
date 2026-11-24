# SunSpec / Modbus TCP: implementation plan

This plan describes how to finish the feature step by step, safely and
verifiably. Do the steps in order. Do not skip a verification step. If a step
fails and the fix is not obvious, stop and report instead of guessing.

Branch: `SunSpec` (based on `main`, version 0.8.156).

---

## 0. Ground rules

1. **Extend only.** Every change to an existing file must sit inside
   `#if defined(ENABLE_SUNSPEC)` / `#endif` (C++) or `<!--IF_ENABLE_SUNSPEC-->` /
   `/*IF_ENABLE_SUNSPEC*/` (HTML/JS). With the flag off, the firmware must be
   byte-for-byte the same in behaviour as `main`.
2. **Read-only.** The Modbus server answers only function codes 0x03 and 0x04.
   Every other function code returns exception 0x01. Do not add write support
   or SunSpec model 123 or 704.
3. **No new libraries.** `AsyncServer` / `AsyncClient` come in through
   ESPAsyncWebServer (ESP8266: ESPAsyncTCP, ESP32: AsyncTCP).
4. **Compile switch:** `//#define ENABLE_SUNSPEC` in `src/config/config.h`
   stays commented out in git. For local tests, enable it in
   `src/config/config_override.h`. That file is git-ignored; never commit it.
5. Do not change `platformio.ini` (no new environments).
6. Never commit to or push to `main`. Commit to `SunSpec` only, with small
   commits, one per step below.

---

## 1. Current state of the branch (draft, not compiled yet)

The code below has been drafted. **It has never been compiled.** The
PlatformIO package registry was blocked in the environment where it was written.

| File | Status | Content |
|---|---|---|
| `src/plugins/SunSpec/ModbusTcpServer.h` | new | Async Modbus TCP server: MBAP framing, FC03/FC04, IP allow list (up to 4 entries, IP or CIDR), max clients, RX timeout 60 s |
| `src/plugins/SunSpec/SunSpec.h` | new | Builds the SunSpec register map on each request: `SunS`, model 1, 101/103, 120, 160 (separate mode only), end model |
| `src/config/config.h` | +3 lines | `//#define ENABLE_SUNSPEC` (disabled) |
| `src/config/settings.h` | extended | `cfgSunSpec_t` in `plugins_t`, `bool sunspec` in `cfgIv_t`, defaults, JSON load/save (`plugin.sunspec`, `iv[].ss`) |
| `src/app.h` / `src/app.cpp` | extended | include, `SunSpecType mSunSpec`, `setup()`, `begin()` in `onNetwork(true)`, module letter `U` |
| `src/web/RestApi.h` | extended | `getSunSpec()` in `/api/setup`, `sunspec` in inverter list, `sunspec` in `save_iv` |
| `src/web/web.h` | extended | parses form fields `ssEn ssMode ssPort ssUnit ssMaxCl ssAllow` in `showSave()` |
| `src/web/html/setup.html` | extended | section "SunSpec / Modbus TCP", checkbox in the inverter dialog (Advanced tab) |
| `src/web/lang.json` | extended | `SUNSPEC_*` and `INV_SUNSPEC` tokens (en/de) |

Design decisions (already agreed with the user):

- **Combined mode** (default, `mode = 0`): one SunSpec device on unit ID
  `unitId` (default 126). It holds the totals of every inverter that is enabled
  and has SunSpec enabled. Serial = `AHOY` + MAC without colons.
- **Separate mode** (`mode = 1`): one SunSpec device per inverter, with unit
  ID = inverter slot + 1. Unit 126 does not answer, so nothing is counted
  twice.
- Values for each inverter: model name `HM-`/`HMS-`/`HMT-`/`MI-` + max power,
  name in Model 1 `Opt`, serial as 12 hex digits in `SN`.

---

## 2. Reference facts (verified in Victron source `victronenergy/dbus-fronius`, Nov 2026)

- The Cerbo GX reads **holding registers (FC03)**. It looks for `SunS` at
  40000, then 50000, then 0.
- By default it probes **unit 126 on port 502**. More `port:unit` pairs can only
  be added through the localsettings value `ModbusAlternates`. The location of
  this setting in the Cerbo GUI is **not verified**.
- **On one IP address it stops after the first newly found device.** It skips
  units it is already connected to. So in separate mode it is likely, but not
  verified, that only one more inverter appears per scan cycle.
- It needs Model 1 with a **non-empty serial** (used as the device ID), plus
  Model 101 or 103. It reads 52 registers of model 101/103 (header included):
  A at offset 2, A_SF 6, PhVphA 10, V_SF 13, W 14, W_SF 15, WH 24-25,
  WH_SF 26, St 38.
- Model 120: it reads WRtg/WRtg_SF (offsets 3/4 with header). Model 160: up to
  6 trackers, DCV at `17+20*i` and DCW at `18+20*i`, counted from model
  start + 3.

---

## 2a. Progress (2026-11-23)

| Step | Status |
|---|---|
| 1 Baseline build | **done** (esp8266-all, esp32-wroom32 pass with the flag off); numbers in `doc/sunspec-plan-results.md`. Byte-identical comparison with `main` not done |
| 2 HTML preprocessor | **done**: `scripts/convertHtml.py`. Checked that the flag-off output of `setup.html` is identical to `main` (ESP8266 and ESP32), and that the JS passes `node --check` with the flag on and off |
| 3 Compile flag on | **partly**: `SunSpec.h` and `ModbusTcpServer.h` compile with g++ `-Wall -Wextra -Werror` against host stubs (`tools/sunspec_host`). all 23 PlatformIO environments build with the flag on, except `esp32-wroom32-prometheus` and `-prometheus-de`, which overflow the flash partition by 109 B / 1333 B (see `doc/sunspec-plan-results.md`); no code changes were needed |
| 4 ESP8266 resources | **partly**: static size delta esp8266-all +1432 B RAM, +8204 B flash (see results). Runtime heap and settings checks need hardware |
| 5 Test client | **done**: `tools/sunspec_read.py`, run against the host build. Combined, separate, exceptions, allow list, max clients, fragmented and pipelined frames all pass |
| 6 Hardware test | **open** |
| 7 Documentation | **done**: `doc/sunspec.md` (screenshots still missing) |
| 8 Cerbo test | **open** (user) |

Fixes made during host testing: both SunSpec headers now include
`config/config.h` themselves, so the `#if defined(ENABLE_SUNSPEC)` check also
works with the switch in `config.h`. Combined mode reports PF as "not
implemented" (an average PF would be misleading).

## 3. Steps

### Step 1: Toolchain and baseline build (flag OFF)

1. `pip install platformio`. Then, in `src/`:
   `pio run -e esp8266-all` and `pio run -e esp32-wroom32`.
2. Write down the `RAM:` and `Flash:` lines of both builds in
   `doc/sunspec-plan-results.md` as the baseline.
3. **Verify:** both builds succeed. Check that `git diff main -- src/` contains
   only additions inside `ENABLE_SUNSPEC` blocks, plus the new files. Command:
   `git diff main -- src/ ':!src/plugins/SunSpec'`. Read every hunk.
4. If the registry is not reachable, stop and tell the user. The build then has
   to run on the user's computer.

### Step 2: Make the HTML preprocessor see the config.h switch

Problem (verified): `scripts/convertHtml.py` only collects flags from
`platformio.ini` build_flags and `src/defines.h`. A `#define ENABLE_SUNSPEC`
in `config.h` or `config_override.h` would compile the C++ code but **hide the
web UI section**.

Change `parseDefinesH()` in `scripts/convertHtml.py` so that it also picks up
`ENABLE_SUNSPEC`, but only this one flag, so that no other behaviour changes:

```python
def parseDefinesH():
    global build_flags
    pattern = r'^\s*#\s*define\s+(\w+)'

    with open("defines.h", "r") as f:
        for line in f:
            match = re.match(pattern, line)
            if match:
                build_flags += [match.group(1)]

    # optional modules which can be enabled by an (uncommented) #define
    # in config/config.h or config/config_override.h
    optional = ["ENABLE_SUNSPEC"]
    for path in ["config/config.h", "config/config_override.h"]:
        if not os.path.isfile(path):
            continue
        with open(path, "r") as f:
            for line in f:
                match = re.match(pattern, line)
                if match and match.group(1) in optional and match.group(1) not in build_flags:
                    build_flags += [match.group(1)]
```

Also make `ENABLE_SUNSPEC` work as a `-D` build flag, which already works
through `getFlagsOfEnv`. Do not change that function.

**Verify:** with the flag off, `src/web/html/h/setup_html.h` is identical to
the baseline (compare the size, or the md5 of the generated file before and
after). With the flag on in `config_override.h`, the generated
`src/web/html/tmp/setup.html` contains `ssEn`.

### Step 3: Compile with the flag ON

1. Create `src/config/config_override.h` containing `#define ENABLE_SUNSPEC`.
2. Build `esp8266-all` and `esp32-wroom32`.
3. Fix compile errors **only in the SunSpec blocks and files**. Likely trouble
   spots to check first:
   - `ModbusTcpServer.h`: include name `ESPAsyncTCP.h` (ESP8266) / `AsyncTCP.h`
     (ESP32). The ESP8266 library comes in through
     `esphome/ESPAsyncWebServer`. Check the real header name in
     `.pio/libdeps/esp8266-all/`.
   - `AsyncClient::setRxTimeout`, `onTimeout`, `space()`, `add()`, `send()`,
     `close(bool)` exist in both libraries. If a signature differs, adapt it.
   - `IPAddress::fromString` and `operator[]` exist on both platforms.
   - `settings.h`: `getVal` / `getChar` with `F("...")` keys. ESP32 and ESP8266
     have different overloads; copy the style of the neighbouring code.
   - `web.h` uses `SUNSPEC_ALLOW_LEN`. Make sure `config/settings.h` is visible
     there (it is included indirectly through `hmSystem.h`; check this).
   - `SunSpec.h` uses `INVERTER_INACT_THRES_SEC`, `INACT_PWR_THRESH`
     (config.h) and `FLD_*`, `CH0`, `IV_*` (hmDefines.h).
4. **Verify:** both builds pass with no new warnings in the SunSpec files.
   Write down RAM/Flash with the flag on in `doc/sunspec-plan-results.md`.

### Step 4: ESP8266 resource check (test device: Wemos D1 mini + 0.66" SSD1306 64x48 shield)

The draft's static RAM is about 1.1 kB: `mMap` 600 B, `mRegs` 250 B and `mTx`
259 B. Each connected client allocates about 270 B on the heap.

1. Compare the flag-on and flag-off builds of `esp8266-all`. Expected growth:
   RAM < 2 kB, flash < 15 kB. If it is much larger, find out why before you
   continue.
2. On ESP8266, `maxClients` is limited to 1-4 by `web.h`. Keep the default at 2.
3. Settings JSON: `saveSettings()` uses
   `DynamicJsonDocument(MAX_ALLOWED_BUF_SIZE)` (the largest free heap block).
   The new keys add about 120 B, plus `"ss":true` for each inverter. Save with
   the maximum number of inverters configured and check that the reload is
   complete. Reboot and check that all inverters are still there.
4. **Verify at runtime** (step 6): `/api/system` free heap and max free block
   with the server enabled, idle and with 2 clients polling every second, for
   at least 30 minutes. There must be no steady decrease (leak check on
   connect/disconnect).

### Step 5: Host test client (no hardware needed for the protocol logic)

Create `tools/sunspec_read.py` (Python 3, standard library only, raw sockets).
It must:

1. Connect to `host:port`, read unit `U` with FC03 at 40000 (2 regs) and
   check `SunS`.
2. Walk the models exactly like the Victron detector: read 2-register headers,
   jump by `L+2`, stop at `0xFFFF`.
3. Decode and print Model 1 (Mn, Md, Opt, Vr, SN, DA), 101/103 (A, V, W,
   Hz, WH, St with scale factors), 120 (WRtg) and 160 (each tracker).
4. Check: model 1 L = 66, model 101/103 L = 50, model 120 L = 26,
   model 160 L = 8 + 20·N, SN not empty. Exit code ≠ 0 on any failure.
5. Options: `--unit`, `--scan 1-32` (separate mode), `--fc 4`.
6. Negative tests: FC06 must return exception 0x01. An address outside the map
   must return 0x02. A wrong unit ID must return 0x0B.

### Step 6: Hardware test on ESP8266 (Wemos D1 mini, display type 5 = SSD1306 64x48)

1. Flash `esp8266-all` with `ENABLE_SUNSPEC` on. In Setup → Display, select
   "SSD1306 0.66" (64x48)" and the I2C pins of the shield. Check the pins in
   the Ahoy pin list; the Wemos shield normally uses D1/D2.
2. **Upgrade test:** flash over an existing config made without SunSpec. All
   old settings must be kept. SunSpec must be off and every inverter must have
   `sunspec = true`.
3. Test matrix (run `tools/sunspec_read.py` for each):

| # | Setting | Expected |
|---|---|---|
| 1 | SunSpec off | port 502 closed |
| 2 | on, combined, unit 126 | unit 126 answers. W = sum of the live page AC power, WH = sum of YieldTotal × 1000 |
| 3 | combined, one inverter's SunSpec checkbox off | that inverter missing from the sum |
| 4 | combined, inverter disabled | missing from the sum |
| 5 | separate | units slot+1 answer, unit 126 → exception 0x0B |
| 6 | allow list `192.168.x.y` (another host) | connection closed immediately |
| 7 | allow list with CIDR including the tester | works |
| 8 | max clients 1, two clients | 2nd one closed |
| 9 | night / inverter unreachable | St = 2 (sleeping), W = 0, WH unchanged |
| 10 | display + SunSpec + MQTT running together for 1 h | no reboot, heap stable |

4. Note the results in `doc/sunspec-plan-results.md`.

### Step 7: Module documentation `doc/sunspec.md`

Contents:

1. Purpose and scope (read-only, SunSpec int+SF models).
2. How to enable: uncomment `#define ENABLE_SUNSPEC` in `src/config/config.h`
   (or add it to `config_override.h`), then build.
3. Web UI settings (each field, default, range). Changes need a reboot.
4. Modes: combined and separate, with unit IDs.
5. The full register map with absolute addresses for combined mode and for one
   separate inverter (with 4 trackers as an example). Generate it from
   `tools/sunspec_read.py` output, not by hand.
6. Value sources (which Ahoy field feeds which SunSpec point). Not implemented
   points.
7. Victron Cerbo GX notes: the facts from section 2. Combined mode is
   recommended. In separate mode, `ModbusAlternates` is needed, and the scan
   limitations apply.
8. Security note: Modbus has no authentication. Use the allow list and keep the
   device in a trusted network.
9. Screenshots of the setup section and the inverter dialog. They are taken
   from the user's install at http://192.168.111.21/ after flashing. Ask the
   user first.

### Step 8: Victron Cerbo GX test (by the user, with support)

1. Combined mode: on the Cerbo, open Settings → PV inverters and start a scan
   (or add the Ahoy IP). Exactly one PV inverter "AhoyDTU <device name>" must
   appear, with power equal to the Ahoy total.
2. Separate mode: add `502:1,502:2,…` to `ModbusAlternates`. Check how many
   inverters appear and after how many scan cycles. Write down the result in
   `doc/sunspec.md`.

### Step 9: Finish

1. Remove `config_override.h` from the working tree (it must not be
   committed).
2. Make sure `config.h` still has `//#define ENABLE_SUNSPEC` (commented out).
3. Build once more with the flag off and check that it matches the step 1
   baseline.
4. Push `SunSpec`. Open a PR to `main` only if the user asks for one.

---

## 4. Open points / risks

- The draft code has **not been compiled yet** (step 3).
- Separate mode on the Cerbo: the scan behaviour is inferred from the code and
  not tested.
- When an inverter has not been reached since boot, its YieldTotal is 0. The
  Cerbo then sees the total energy jump. Option: hide the device until its
  first data arrives. Decide with the user.
- Combined mode always uses model 101 (single phase), also with HMT
  inverters. The Cerbo puts the power on the phase that is configured there.
