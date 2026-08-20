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

### Hardware (Phases 0–1)

None of the power hardware is needed yet — no battery, no boost, no cable
splice. Just:

- Seeed XIAO ESP32-S3 (ships without headers; you'll solder to `5V`, `GND`, `D6`, `D7`)
- Corne v4.1
- An ordinary, unmodified USB-C to USB-C cable
- A **5 V source that can also speak UART**, either:
  - a **Raspberry Pi Zero 2 W** — covers 5 V, console and flashing in one, no
    level shifting needed (both sides are 3.3 V logic), or
  - a **USB-UART adapter** (CP2102 / CH340 / FT232) plus a separate 5 V supply —
    a bench supply with a ~700 mA current limit is safest
- Multimeter

See [`docs/hardware.md` §2](docs/hardware.md) for the full bench rig, wiring
diagram and cautions.

### Toolchain

ESP-IDF **v5.3 or newer**:

```sh
mkdir -p ~/esp && cd ~/esp
git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
. ./export.sh          # source this in every new shell
```

Don't try to build this on a Pi Zero 2 W — 512 MB RAM against a toolchain that
wants 1–2 GB means swap thrashing and SD wear. Build on a PC, flash from the Pi.

---

## Build & flash

```sh
cd wireless_corne
idf.py set-target esp32s3
idf.py build
```

### Console and flashing both go over UART

USB-OTG and USB-Serial-JTAG **share GPIO19/20** on the ESP32-S3 and only one can
be muxed at a time, so the USB serial port disappears the moment host mode comes
up. Rather than swapping the USB-C cable between PC and keyboard on every
iteration, do both flashing and monitoring over UART0 and leave the USB-C port
permanently dedicated to the Corne.

| XIAO pin | GPIO | Function | Connect to |
|---|---|---|---|
| D6 | 43 | UART0 TX | Pi pin 10 (GPIO15 RXD) / adapter **RX** |
| D7 | 44 | UART0 RX | Pi pin 8 (GPIO14 TXD) / adapter **TX** |
| 5V | — | power in | Pi pin 2 (5V) / 5 V supply |
| GND | — | ground | Pi pin 6 (GND) / supply GND |

115200 8N1. Note the TX/RX crossover.

Enter download mode by holding **BOOT**, tapping **RESET**, releasing **BOOT** —
a plain GPIO UART has no DTR/RTS, so esptool can't auto-reset. Then:

```sh
esptool.py --chip esp32s3 --port /dev/ttyAMA0 --baud 460800 \
  --before no_reset --after hard_reset write_flash \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0x10000 wireless_corne.bin

screen /dev/ttyAMA0 115200
```

**The ESP32-S3 bootloader goes at `0x0`, not `0x1000`** — that offset differs
from ESP32/ESP32-S2 and is a common way to make a first flash fail confusingly.

If you'd rather flash over USB instead: hold BOOT, tap RESET, then
`idf.py -p /dev/ttyACM0 flash`. But you'll be unplugging the keyboard every time.

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

Wire up the bench rig ([`docs/hardware.md` §2](docs/hardware.md)) and connect
the Corne. Each HID interface should print a decoded report descriptor and a
summary table like:

```
--- report summary for iface 0 (Boot/Keyboard) ---
  Uses Report IDs: NO (single unnumbered report)
  Report ID    Input bytes    Output bytes   Feature bytes
  (none)       8              1              0
```

Those tables are the direct input to the Phase 3 BLE report map. Capture the
full log — the raw hex dumps let the descriptors be re-parsed offline.

> **Turn RGB off on the Corne before this test.** If your 5 V comes from a
> Raspberry Pi's GPIO pins, the Corne's RGB current comes straight out of the
> Pi's supply budget, and a brownout corrupts the SD card rather than just
> rebooting. HID enumeration doesn't need RGB. Use the OLED as your power
> indicator instead.

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
