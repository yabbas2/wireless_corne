# wireless_corne

Turn a wired **Corne v4.1** into a Bluetooth keyboard using a **Seeed XIAO
ESP32-S3** as a USB-host → BLE HID bridge, without modifying the keyboard's
firmware in any meaningful way.

The ESP32 plugs into the Corne over USB-C, reads its HID reports as a USB host,
and re-transmits them over BLE. Power for both boards comes from a LiPo + boost
converter in the same pack.

> **Read [`docs/hardware.md`](docs/hardware.md) before powering anything on.**
> In particular, the boost converter's trimpot must be set to 5.00 V under load
> and verified with a meter *before* the keyboard is ever connected.

---

## Why this approach

Corne v4.1 has an **RP2040 soldered directly to the PCB** (confirmed in QMK's
`keyboards/crkbd/rev4_1/info.json`: `"processor": "RP2040"`). There is no
socketed controller, so the usual "swap in a nice!nano and flash ZMK" route is
impossible. A USB-host bridge is the only option that keeps the existing PCBs.

The honest trade: a native ZMK split sleeps between keystrokes and runs for
weeks. This design has **no sleep at all** — the RP2040s run QMK continuously
and the USB host stack polls at 1 ms — so it runs for **hours**. See the power
budget in `docs/hardware.md`.

---

## Status

| Phase | Description | State |
|---|---|---|
| 0 | Project scaffold, UART console | **done (untested on hardware)** |
| 1 | USB host enumeration + report descriptor dump | **done (untested on hardware)** |
| 2 | Minimal BLE HID, basic typing | not started |
| 2.5 | **EMI go/no-go gate** | not started |
| 3 | Full report map: NKRO, consumer, mouse, LED output reports | not started |
| 4 | Multi-profile bonding + F13/F14/F15 switching | not started |
| 5 | Power stack, current measurement, thermal check | not started |
| 6 | Battery ADC, low-battery warning, enclosure | not started |

**Nothing in this repo has run on hardware yet.** ESP-IDF was not installed on
the development machine, so the firmware has not been compiled. The one
exception is the HID report descriptor parser, which is dependency-free and *is*
verified — see [Testing](#testing).

---

## Prerequisites

### Hardware

- Seeed XIAO ESP32-S3
- Corne v4.1
- **USB-UART adapter** (CP2102 / CH340 / FT232) — required, see below
- Multimeter

### Toolchain

ESP-IDF **v5.3 or newer**:

```sh
mkdir -p ~/esp && cd ~/esp
git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
. ./export.sh          # source this in every new shell
```

---

## Build & flash

```sh
cd wireless_corne
idf.py set-target esp32s3
idf.py build
```

### Flashing

USB-OTG and USB-Serial-JTAG **share GPIO19/20** on the ESP32-S3 and only one can
be muxed at a time. Once the firmware brings up USB host mode, the USB serial
port disappears. To flash:

1. Hold **BOOT**
2. Tap **RESET**
3. Release **BOOT** — the board is now in ROM download mode, USB serial is back
4. `idf.py -p /dev/ttyACM0 flash`

### Console

Because of the same mux conflict, logs go out over **UART0**, not USB:

| XIAO pin | GPIO | Function | Connect to |
|---|---|---|---|
| D6 | 43 | UART0 TX | adapter **RX** |
| D7 | 44 | UART0 RX | adapter **TX** |
| GND | — | GND | adapter GND |

115200 8N1.

```sh
idf.py -p /dev/ttyUSB0 monitor        # the UART adapter, not the XIAO
```

---

## Testing

`main/hid_dump.c` is deliberately free of ESP-IDF dependencies so the report
descriptor parser can be built and exercised with a normal compiler:

```sh
make -C test/host run
```

This checks two descriptors with known-good expected output: the canonical USB
HID boot keyboard (no Report IDs; 8-byte input, 1-byte output) and a
multi-Report-ID system+consumer control layout matching QMK's extrakey interface
(3 bytes each). Builds under `-Wall -Wextra -Wpedantic -Werror`.

Run this after any change to `hid_dump.c`.

---

## Layout

```
CMakeLists.txt          top-level ESP-IDF project
sdkconfig.defaults      console-on-UART0 and other project defaults
main/
  main.c                app_main, NVS init, banner, heartbeat
  usb_kbd.c/.h          USB host: enumerate, Set_Protocol(REPORT), read reports
  hid_dump.c/.h         HID report descriptor parser + pretty printer
test/host/              host-side test for the parser (no hardware needed)
docs/hardware.md        wiring, calibration procedure, power budget, risks
```

---

## Phase 1: what to expect

Power the rig from a bench supply or power bank (no battery/boost yet), attach
the UART adapter, and connect the Corne. Each HID interface should print a
decoded report descriptor and a summary table like:

```
--- report summary for iface 0 (Boot/Keyboard) ---
  Uses Report IDs: NO (single unnumbered report)
  Report ID    Input bytes    Output bytes   Feature bytes
  (none)       8              1              0
```

Those tables are the direct input to the Phase 3 BLE report map. Capture the
full log — the raw hex dumps let the descriptors be re-parsed offline.

### A note on interface enumeration

Espressif's upstream example only opens interfaces where
`proto != HID_PROTOCOL_NONE`. That would be wrong here: QMK reports
`HID_PROTOCOL_NONE` for its mouse/extrakey interface, so following the example
would silently drop media keys and mouse support. This project opens **every**
HID interface and lets Phase 3 decide what to forward.

---

## References

- [foostan/crkbd](https://github.com/foostan/crkbd) — Corne hardware
- [QMK `keyboards/crkbd/rev4_1`](https://github.com/qmk/qmk_firmware/tree/master/keyboards/crkbd/rev4_1)
- [`espressif/usb_host_hid`](https://github.com/espressif/esp-usb/tree/master/host/class/hid/usb_host_hid) v1.2.0 — API this code targets
- [XIAO ESP32-S3 wiki](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- [crkbd issue #265](https://github.com/foostan/crkbd/issues/265) — the v4.x EMI bug
