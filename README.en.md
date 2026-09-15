**English** | [한국어](README.md)

# T2CAN-ROAMING

[![Release](https://img.shields.io/github/v/release/anoblekman/t2can-roaming?sort=semver)](https://github.com/anoblekman/t2can-roaming/releases/latest)
[![License: GPL-3.0](https://img.shields.io/github/license/anoblekman/t2can-roaming)](LICENSE)
[![Board: LilyGo T-2CAN](https://img.shields.io/badge/board-LilyGo%20T--2CAN%20(ESP32--S3)-blue)](https://www.lilygo.cc/)
[![Build: PlatformIO](https://img.shields.io/badge/build-PlatformIO-orange)](https://platformio.org/)

Firmware for a **LilyGo T-2CAN** board (ESP32-S3, dual CAN) that attaches to a
personally-owned **Tesla Model 3 Highland (HW4)** and adjusts a few driver-assist
and regional behaviors on the owner's own vehicle. It is a T-2CAN-specific rewrite
of [`hypery11/flipper-tesla-fsd`](https://github.com/hypery11/flipper-tesla-fsd).

This is a personal / research vehicle-modification project. It is not a product,
not affiliated with Tesla, and comes with **no warranty of any kind**.

---

## ⚠️ SAFETY & DISCLAIMER — READ FIRST

**Research, educational, and personal use only.** Use this only on a vehicle you
own, and only where doing so is legal — off public roads where required by your
jurisdiction. **THERE IS NO WARRANTY. USE AT YOUR OWN RISK.** You are solely
responsible for the safe and lawful operation of your vehicle.

- **This firmware can defeat a driver-attention ("hands-on") monitoring feature.**
  Disabling that nag does **not** make the car autonomous. The driver remains fully
  responsible for staying attentive, keeping hands on the wheel, and controlling the
  vehicle at all times. Do not rely on the car to drive itself.
- You are responsible for legal and regulatory compliance in your jurisdiction.
  Driver-assist behavior, "hands-on" requirements, and region locks differ by market
  and by law.
- Modifying vehicle bus traffic can have unintended effects on safety systems.
  Test conservatively, in a safe place, with an immediate way back to a safe state.

### ⛔ Hard safety rule discovered in testing — do NOT ignore

**NEVER add CAN frame `0x370` to the CAN B (Chassis) acceptance filter.**

During real driving, echoing `0x370` onto the **Chassis** bus was observed to
coincide with the car latching Autopilot into an **invalid state** and
**regenerative braking switching off** (dashboard warning). The Chassis-bus hardware
filter must admit **only `0x39B` + `0x286`** — nothing else, and never `0x370`.
There is an explicit warning comment guarding this in
[`src/can_driver.cpp`](src/can_driver.cpp) (search for `0x370`); do not remove it.

`0x370` is echoed only onto the **Party** bus (CAN A), which is the correct and
tested path for the nag-removal feature.

### Default-listen safety model

Both CAN controllers **boot in hardware Listen-Only mode** and physically cannot
transmit. Nothing is ever injected until you explicitly switch the device to
**Active**. When Active, injection is still gated: the injection gate only opens
when Autopilot is engaged (`DAS_autopilotState` 3–5) and has been stable for ≥ 1 s;
**state 6 (in-car Autopark) is blocked**, so the firmware will not inject during an
in-car parking maneuver.

---

## Quick start

1. Flash the firmware — a `.bin` from [Releases](https://github.com/anoblekman/t2can-roaming/releases/latest), or a source build (see "Build & flash" below).
2. Connect to the Wi-Fi network **`T2CAN-ROAMING`** (default password **`passwd`**).
3. Open **`http://192.168.4.1`** in a browser.
4. **Change the password immediately.** In the dashboard go to **Tools → Network settings** and set a new SSID/password (WPA2, 8–63 chars). The AP password is this device's only access protection.
5. It boots in **Listen** mode (silent, transmits nothing); switch to **Active** to inject.

---

## Features

| Feature | Frame | What it does |
| --- | --- | --- |
| **Steering nag removal** | `0x370` echo (EPAS3S_sysStatus) | Suppresses the Autosteer "hold the wheel" nag by echoing the torque / `handsOnLevel` fields. Echoed **only on the Party bus (CAN A)**. |
| **Summon EU distance unlock** | `0x3FD` mux1 (bit19 / bit47) | Unlocks the EU ~6 m distance limit on Smart Summon so the car can be summoned from farther away. |
| **0x3F8 experiment harness** | `0x3F8` (UI_driverAssistControl) | A gated, opt-in experiment harness for probing individual `0x3F8` bits. Off by default; each experiment is a per-bit toggle. |

Plus supporting infrastructure:

- **Gate-decision trace logging** (always on) — records why the injection gate
  opened or stayed closed, persisted to LittleFS across reboots.
- **Wi-Fi AP web dashboard** — a self-hosted SoftAP + web UI for status, mode
  control, network settings, and diagnostics.
- **OTA updates** — over the web dashboard (Wi-Fi), or over USB serial.

---

## Hardware & wiring

- **Board:** LilyGo **T-2CAN** (ESP32-S3, dual CAN: an **MCP2515** over SPI plus the
  ESP32-S3 built-in **TWAI** controller).
- **Vehicle connector:** Tesla 20-pin **X179**.
- **Bus mapping (500 kbps both):**

| Controller | Bus | X179 pins |
| --- | --- | --- |
| **CAN A — MCP2515** | **Party** bus | pins **2 / 3** |
| **CAN B — TWAI** | **Chassis** bus | pins **13 / 14** |

The Chassis-bus (CAN B) acceptance filter is deliberately restricted to `0x39B` +
`0x286` only — see the SAFETY section and `src/can_driver.cpp`.

The ESP32-S3 pin map (MCP2515 SPI pins, TWAI TX/RX, BOOT button) lives in
[`src/config.h`](src/config.h).

---

## Build & flash

### Download a pre-built binary

Grab the latest firmware from the **[Releases page](https://github.com/anoblekman/t2can-roaming/releases/latest)** — no build required:

| File | Use |
|------|-----|
| `t2can-roaming-<version>-ota.bin` | Web-dashboard OTA (app partition) |
| `t2can-roaming-<version>-full.bin` | Serial full-flash: `esptool.py --chip esp32s3 write_flash 0x0 <file>` |

Or build from source below.

Built with **PlatformIO**:

```
pio run -e lilygo-t2can
```

A `version_gen.py` pre-build script stamps the firmware version as `YYYYMMDD.N`
(build date + that day's build count), so a trace header uniquely identifies the
build it came from.

The build emits **two** images — do not confuse them:

| File | Contents | Use |
| --- | --- | --- |
| `firmware.bin` | **application image only** | Web OTA upload / write to the app partition |
| `firmware-merged.bin` | bootloader + partition table + boot_app0 + app (full flash image at **offset 0x0**) | First-time / full-recovery flash via ESP Web Tools or `esptool` |

### (a) USB cable (recommended, recovery path)

```
pio run -e lilygo-t2can -t upload
```

### (b) Web OTA (no cable)

1. Join the board's AP (below).
2. Dashboard → **Tools → Firmware update → choose file**.
3. Select **`firmware.bin`** (not `firmware-merged.bin` — writing a full-flash image
   into the app partition fails).
4. After 100% the board reboots automatically; refresh after ~30 s.

While an OTA upload is in progress, **CAN transmit is automatically halted** (the
buses are physically dropped to Listen-Only while flash is written), and restored on
failure/cancel. OTA validates the `.bin` (ESP32 image magic byte `0xE9`, target
partition size) before writing; a bad image is rejected and the current firmware is
kept. USB re-flash always remains the recovery path.

---

## Usage

### Join the AP

- **SSID:** `T2CAN-ROAMING` (default)
- **Password:** `passwd` (default, stored in NVS)
- **Dashboard:** `http://192.168.4.1`

> **Change the defaults.** The AP password (WPA2) is the *only* trust boundary — once
> a client is on the AP, the dashboard, OTA, and settings are reachable with no
> further HTTP auth (this is a private garage device; access is controlled by the
> Wi-Fi password alone). **Change the SSID and password** under **Tools → Network
> settings**. The AP password must be 8–63 characters (WPA2). A 5-second BOOT
> long-press within 20 s of boot factory-resets NVS back to the defaults.

### Listen → Active

| Mode | Behavior | Safety |
| --- | --- | --- |
| **Listen** (default at boot) | Receive only. Nothing is placed on the bus. | Bus physically silent |
| **Active** | Frames that pass the gate are transmitted (injected / echoed). | Injection occurs |

Switch to Active from the dashboard's **Listen / Active** control, or by a **short
press of the BOOT button** (GPIO0). A short press toggles the mode (and saves it to
NVS); the 20 s / 5 s BOOT long-press does the factory reset.

### Diagnostics

- **Gate trace** (always on): download or clear the gate-decision log from
  **Tools → Gate log** (`/trace/get`, `/trace/clear`). Persists across reboots.
- **Frame dump** (manual): capture raw CAN frames to a PSRAM ring buffer via
  **Tools → CAN dump start/stop**, then download. Lost on power-off.

---

## Known limitations

- **Nag removal does not work on 2026 Model 3 Highland.** It is confirmed working on a
  **2024** Model 3 Highland, but a **2026** Model 3 Highland — tested on *identical*
  software (`2026.20.6.1`) — does **not** accept the `0x370` spoof. The root cause is
  a **model-year / hardware-generation difference, not software**. This is an open
  problem shared by the wider community for 2026 builds; no working `0x370` approach is
  known for them yet.
- **The reference DBC is not year-specific.** `opendbc`'s `tesla_model3_party.dbc` is
  the best public reference, but it officially covers up to **MY2025**. Frame IDs and
  bit layouts on the real car are ground truth; treat DBC values as a starting point,
  not gospel.
- Some behaviors (e.g. certain Summon signal fields and the `0x3F8` experiments) are
  still being validated on-vehicle. The `0x3F8` harness is experimental by design.

---

## Repository layout

- `logic/` — portable, host-testable core (frame parsing, gate/state machine,
  checksums, nag / Summon / `0x3F8` builders). Pure C, no hardware dependencies.
- `src/` — ESP32-S3 firmware: CAN drivers, Wi-Fi AP, web UI, OTA, trace log, prefs.
- `test/` — host unit tests for the `logic/` core (`test/run-tests.sh`).
- `platformio.ini` — PlatformIO project / environment.
- `version_gen.py`, `merge_firmware.py` — PlatformIO build scripts.
- `docs/upstream-comparison.md` — cross-check of frames/bits against `opendbc` and
  other public Tesla CAN projects.

---

## Credits & acknowledgements

- **[flipper-tesla-fsd](https://github.com/hypery11/flipper-tesla-fsd)** — the
  upstream project this is a T-2CAN-specific rewrite of.
- **[ev-open-can-tools](https://github.com/ev-open-can-tools/ev-open-can-tools)** —
  reference tooling used to cross-check signal definitions and injection policy.
- **[opendbc](https://github.com/commaai/opendbc)** (commaai) — the reference DBC for
  HW4 / Party-bus frame IDs, bit layouts, and the additive checksum; used to
  independently cross-check signals against this implementation.

Tesla and Model 3 are trademarks of their respective owner. This project is not
affiliated with, endorsed by, or supported by Tesla.

---

## License

**GPL-3.0.** `hypery11/flipper-tesla-fsd`, from which this is derived, is licensed
under GPL-3.0, so this rewrite is released under the same license. See
[`LICENSE`](LICENSE) for the full text.
