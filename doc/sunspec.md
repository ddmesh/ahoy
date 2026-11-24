# SunSpec / Modbus TCP module

Ahoy can provide its inverter data as **SunSpec** over **Modbus TCP**, so that
energy management systems such as the Victron Cerbo GX can read it. The module
is **read-only**: it never changes inverter settings and has no power limit
control.

The module is optional and **not compiled by default**.

## Enable the module (compile time)

Do one of the following, then build and flash:

- uncomment the line `//#define ENABLE_SUNSPEC` in `src/config/config.h`, or
- add `#define ENABLE_SUNSPEC` to `src/config/config_override.h`. That file is
  local and not tracked by git.

The build script also picks up the define from these two files. When it is set,
the web UI shows the settings section. The firmware then lists the module
letter `U` in System → build options.

### Optional models (compile time)

By default only the minimum is served: model 1 (device info) and model
101/103 (power, voltage, current per phase, yield, state). Two optional models
can be added with a define in the same place as `ENABLE_SUNSPEC`:

| Define | Model | Content | Size |
|---|---|---|---|
| `SUNSPEC_MODEL_120` | 120 nameplate | Max. power (the Cerbo shows it as max. power of the PV inverter) | 28 registers |
| `SUNSPEC_MODEL_160` | 160 multiple MPPT | Current, voltage, power and yield of each PV input (separate mode only) | up to 130 registers |

The register buffer is sized to the enabled models (124, 152 or 282
registers). Measured sizes are in `doc/sunspec-plan-results.md`.

Resources (static, without clients, minimal build): about 1.1 kB RAM and 8 kB flash on ESP8266. Each connected client
uses about 0.3 kB of heap.

## Settings (web UI)

Settings → **SunSpec / Modbus TCP**:

| Field | Default | Description |
|---|---|---|
| Enable SunSpec Modbus TCP server | off | Starts the server once the network is up |
| Mode | Combined | **Combined**: one device with the totals of the whole system. **Separate**: one device per inverter |
| Port | 502 | TCP port |
| Unit ID (combined mode) | 126 | Modbus unit ID of the combined device (1-247) |
| Max. connections | 2 | Simultaneous clients (1-4). Extra connections are closed |
| Allowed clients | empty (all) | Up to 4 entries, IP or network in CIDR form, comma separated, e.g. `192.168.1.10, 192.168.2.0/24` |

Changes take effect after a **reboot**.

For each inverter: Settings → Inverter → edit → **Advanced**:

- "Provide via SunSpec / Modbus TCP" (default: on). An inverter is included
  only if it is **enabled** and this checkbox is set.
- "Grid phase" L1 / L2 / L3 (default: L1). Only used for single phase
  inverters in combined mode, see [Phases](#phases). HMT inverters always feed
  all three phases.

## Phases

Hoymiles single phase inverters (HM, HMS, MI) do **not** report which grid
phase they are connected to. Their AC voltage is just the voltage of that
phase, so Ahoy cannot find out the phase by itself. Set it per inverter
("Grid phase").

| Mode | Included inverters | SunSpec device | Phase assignment |
|---|---|---|---|
| Combined | all single phase, all on the same phase | Model 101 | Set the phase of this PV inverter on the Cerbo (Settings → PV inverters) |
| Combined | single phase on different phases, or any HMT | Model 103 | Per phase current and voltage from the Ahoy phase settings |
| Separate | single phase inverter | Model 101 | Set the phase of each PV inverter on the Cerbo. The Ahoy phase setting is not used |
| Separate | HMT | Model 103 | Phases from the inverter |

Example: inverter A (HM-800) on L1 and inverter B (HMS-400) on L2, combined
mode. Ahoy serves model 103 with A on L1, B on L2, 0 A / 0 V on L3. The
Cerbo splits the total power over the phases by voltage × current of each
phase (checked in dbus-fronius `DataProcessor::process`), so the per phase
power matches the two inverters. A phase without a producing inverter is
reported with 0 V instead of "not implemented", because the Cerbo would
otherwise compute no per phase power at all.

The model is chosen from the settings, not from which inverters are
currently producing, so it does not change between day and night. Changing a
phase setting can switch the combined device between model 101 and 103. The
Cerbo then detects the device again.

## Modes and unit IDs

| Mode | Unit IDs that answer | Content |
|---|---|---|
| Combined | `Unit ID` setting (default 126) | Totals of all included inverters |
| Separate | inverter number + 1 (first inverter in the list = 1) | One device per included inverter |

Any other unit ID gets Modbus exception `0x0B` (gateway target failed). In
separate mode the combined device does not exist, so the values are never
counted twice.

## Protocol

- Function codes **0x03** (read holding registers) and **0x04** (read input
  registers), max. 125 registers per request. Both return the same data.
- Every other function code gets exception `0x01`. Addresses outside the map
  get `0x02`. A register count of 0 or more than 125 gets `0x03`.
- The SunSpec base address is **40000** (protocol address, 0-based).
- Values use SunSpec "integer + scale factor" encoding. Points that are not
  implemented hold `0x8000` (int16 and scale factors), `0xFFFF` (uint16) or
  `0` (acc32).
- Scale factors can change at runtime: a value that would not fit into 16 bits
  gets a larger scale factor. Always read each value together with its scale
  factor.

## Register map

### Common part (both modes)

| Address | Model / point | Content |
|---|---|---|
| 40000-40001 | `SunS` | SunSpec marker |
| 40002 / 40003 | Model **1** ID / length (66) | |
| 40004-40019 | Mn | Combined: `AhoyDTU`. Separate: `Hoymiles` |
| 40020-40035 | Md | Combined: Ahoy device name. Separate: model, e.g. `HM-800`, `HMS-2000`, `HMT-2250` (generation + max. power) |
| 40036-40043 | Opt | Combined: `total`. Separate: **inverter name** from the Ahoy settings |
| 40044-40051 | Vr | Combined: Ahoy version. Separate: inverter firmware version |
| 40052-40067 | SN | Combined: `AHOY` + MAC address. Separate: inverter **serial number** (12 hex digits) |
| 40068 | DA | Unit ID |
| 40070 / 40071 | Model **101** (single phase) or **103** (three phase), see [Phases](#phases). ID / length (50) | |
| 40072 | A | AC current (sum) |
| 40073-40075 | AphA, AphB, AphC | Phase currents (B/C only for model 103). Combined: sum per phase |
| 40076 | A_SF | |
| 40077-40079 | PPVph* | not implemented |
| 40080-40082 | PhVphA, PhVphB, PhVphC | Phase voltage (B/C only for model 103). Combined: average of the producing inverters on that phase, 0 if none |
| 40083 | V_SF | -1 |
| 40084 / 40085 | W / W_SF | AC power |
| 40086 / 40087 | Hz / Hz_SF | Grid frequency (combined: average) |
| 40088 / 40089 | VA / VA_SF | not implemented |
| 40090 / 40091 | VAr / VAr_SF | Reactive power |
| 40092 / 40093 | PF / PF_SF | Power factor in % (separate mode only) |
| 40094-40095 | WH (acc32) | Total yield in Wh |
| 40096 | WH_SF | 0 |
| 40097-40100 | DCA, DCV + SF | not implemented (see model 160) |
| 40101 / 40102 | DCW / DCW_SF | DC power (sum of all inputs) |
| 40103 | TmpCab | Inverter temperature (combined: maximum) |
| 40107 | Tmp_SF | |
| 40108 | St | Operating state, see below |
| 40110-40121 | Evt1, Evt2, EvtVnd* | always 0 |

Without optional models the end model (`0xFFFF`, `0`) follows at **40122**.

### Optional: Model 120 (nameplate, `SUNSPEC_MODEL_120`)

| Address | Point | Content |
|---|---|---|
| 40122 / 40123 | Model **120** ID / length (26) | |
| 40124 | DERTyp | 4 (PV) |
| 40125 / 40126 | WRtg / WRtg_SF | Max. power (combined: sum) |
| 40127 / 40128 | VARtg / VARtg_SF | same as WRtg |

With model 120, combined mode ends with the end model at **40150**.

### Optional, separate mode only: Model 160 (multiple MPPT, `SUNSPEC_MODEL_160`)

The addresses below assume model 120 is also enabled. Without it, subtract 28.

| Address | Point | Content |
|---|---|---|
| 40150 / 40151 | ID 160 / length 8 + 20·N | |
| 40152-40155 | DCA_SF, DCV_SF, DCW_SF, DCWH_SF | Scale factors for all inputs |
| 40158 | N | Number of DC inputs (1-6) |
| 40160 + 20·i | ID | Input number (i = 0 … N-1) |
| 40161-40168 + 20·i | IDStr | Input name from the Ahoy settings, or `PV1` … `PV6` |
| 40169 + 20·i | DCA | DC current |
| 40170 + 20·i | DCV | DC voltage |
| 40171 + 20·i | DCW | DC power |
| 40172-40173 + 20·i | DCWH | Input yield total in Wh |
| 40177 + 20·i | DCSt | 4 (MPPT) when the inverter is reachable, otherwise 2 |

The end model follows at 40152 + 8 + 20·N. Example: 40200 for a 2-input
inverter, 40280 for an HMT with 6 inputs.

### Operating state (St)

| Value | Meaning | When |
|---|---|---|
| 2 | Sleeping | No data from the inverter for 5 minutes (e.g. at night) |
| 4 | MPPT | Producing |
| 5 | Throttled | Producing with power limit < 100 % |
| 8 | Standby | Reachable but 0 W |

Combined mode: Throttled if any inverter is throttled, otherwise MPPT if any
inverter is producing, and so on.

### Value sources

| SunSpec | Ahoy field (live data) |
|---|---|
| W | `P_AC` |
| A / AphA-C | `I_AC` / `I_AC_1..3` (HMT) |
| PhV | `U_AC` / `U_AC_1N..3N` (HMT) |
| Hz | `F_AC` |
| VAr | `Q_AC` |
| PF | `PF_AC` |
| WH | `YieldTotal` × 1000 |
| DCW | `P_DC` (sum) |
| TmpCab | `Temp` |
| WRtg | max. power from the inverter's hardware part number |

When an inverter is not reachable, W, A, V and the DC values are 0. WH keeps
the last known total. After a reboot, an inverter that has not answered yet
shows WH = 0.

## Victron Cerbo GX

The following was checked in Victron's driver source
([dbus-fronius](https://github.com/victronenergy/dbus-fronius), Nov 2026). It
has **not been tested on a real Cerbo yet**.

- The Cerbo looks for SunSpec devices on **port 502, unit 126**. It needs model
  1 with a serial number and model 101 or 103. It also uses model 120 and model
  160 when they are present (optional here, see above).
- **Combined mode is recommended**: the whole Ahoy system then shows up as one
  PV inverter ("AhoyDTU <device name>"). Per phase values: see
  [Phases](#phases).
- **Separate mode**: the Cerbo only probes other unit IDs when they are listed
  as `port:unit` pairs in its `ModbusAlternates` setting (e.g.
  `502:1,502:2,502:3`). Also, on one IP address it adds only one new device per
  scan. How many inverters finally appear, and after how long, still has to be
  tested.

## Security

Modbus TCP has no authentication or encryption. Anyone who can reach the port
can read the data. Use the **allowed clients** list and keep Ahoy in a trusted
network. Do not forward the port to the internet.

## Testing

- `tools/sunspec_read.py <ip> [--unit 126] [--scan 1-32] [--negative]` reads
  and checks the model chain the same way the Cerbo does. It needs only the
  Python 3 standard library.
- `tools/sunspec_host/run.sh` compiles the module on a Linux PC with stubs
  (no ESP toolchain), minimal and with models 120 / 160, starts it on
  localhost and runs the test client in both modes and with one, two and three
  phases.

## Files

| File | Purpose |
|---|---|
| `src/plugins/SunSpec/ModbusTcpServer.h` | Modbus TCP server (framing, function codes, allow list, connection limit) |
| `src/plugins/SunSpec/SunSpec.h` | SunSpec register map, combined and separate mode |
| `src/config/settings.h` | `plugin.sunspec` settings, `iv[].ss` and `iv[].ssph` (phase) per inverter |
| `src/app.h`, `src/app.cpp` | Setup and start of the module |
| `src/web/RestApi.h`, `src/web/web.h`, `src/web/html/setup.html`, `src/web/lang.json` | Web UI |
| `scripts/convertHtml.py` | Shows the web UI section when `ENABLE_SUNSPEC` is defined in `config.h` |
