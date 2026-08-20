# Hardware

Everything here is derived from datasheets and vendor documentation, not from
measurements. Verify with a meter before trusting it.

---

## 1. Overview

```
[Left half] ──TRRS──> [Right half]
     │  whichever half receives VBUS becomes QMK master (USB_VBUS_PIN GP13)
     │
     │ spliced USB-C cable
     ▼
[XIAO ESP32-S3]  USB host (data) ──> HID translation ──> BLE HID
     ▲                                                       │
     │ 5 V                                                   ▼
[MT3608 boost] <── [switch] <── [2000 mAh LiPo] <── [TP4056]      [PC 1..3]
```

The halves stay wired to each other over TRRS. "Wireless" means wireless to the
PC only.

---

## 2. Bill of materials

| Part | Spec | Size | Notes |
|---|---|---|---|
| XIAO ESP32-S3 | ESP32-S3R8, 8 MB flash | 21 x 17.8 mm | |
| LiPo | 2000 mAh, 103450, **with PCM** | 50 x 34 x 10 mm | PCM is the only over-discharge cutoff in this design |
| Charger + boost | TP4056 (Rprog 1.2 k = 1 A) + MT3608 | ~26 x 17 mm | Combo module |
| Bulk cap | 220 µF low-ESR | | On the 5 V rail |
| Switch | SPDT slide | | See §4 for placement |
| USB-C cable | USB 2.0, **non-e-marked**, ~15 cm | | Sacrificial |
| Resistors | 2 x 100 kΩ | | Battery ADC divider (Phase 6) |
| USB-UART adapter | CP2102 / CH340 / FT232 | | **Required for all firmware work** |

---

## 3. USB cable splice

The XIAO's single USB-C port does three jobs at once: host data, power in, and
power pass-through to the keyboard.

```
   XIAO USB-C                                        Corne USB-C
  ┌──────────┐                                      ┌──────────┐
  │ D+ GPIO20├────────────── data ──────────────────┤ D+       │
  │ D- GPIO19├────────────── data ──────────────────┤ D-       │
  │ VBUS     ├────────●───── 5 V ───────●───────────┤ VBUS     │
  │ GND      ├────────●───── GND ───────●───────────┤ GND      │
  │ CC1/CC2  │        │   (not connected)           │ CC1/CC2  │
  └──────────┘        │                             └──────────┘
                 MT3608 5 V out
```

**Do not sever the cable.** Window the jacket mid-length, expose the VBUS (red)
and GND (black) conductors, and solder the boost output onto them. D+/D− stay
sealed inside. Fewer joints, and no opportunity to mix up the data pair.

### Why CC is not connected

CC exists so two USB-C devices can negotiate which one sources VBUS. We have
hardwired that decision — the boost is the source, always. Separately, the
ESP32-S3's host/device role is selected in software by `usb_host_install()`, not
by any hardware role-detection pin. So CC is irrelevant here.

### Why the XIAO cannot power the keyboard itself

Per Seeed's own documentation:

> "When you use battery power, there will be no voltage on the 5V pin."

The XIAO has a charger and a 3.3 V LDO but no boost converter. It physically
cannot put 5 V on VBUS. So unlike a normal USB host, the XIAO here is a power
**consumer**; the boost feeds the XIAO and the Corne in parallel.

### Rule: battery never touches the XIAO's B+/B− pads

The battery lives only on the external TP4056. If it were also on the XIAO's
B+/B− pads, the boost would drain the cell while the XIAO's onboard BQ25101
charged it back — a circulating current that wastes power for nothing. Leaving
the battery off the XIAO removes the problem entirely and also avoids its
uselessly slow 100 mA charger.

---

## 4. Switch placement

The TP4056 and MT3608 share the same BAT node on the module, so switch
placement determines whether charging still works with the load off.

| | Placement | Charge with load off? | Idle drain | Notes |
|---|---|---|---|---|
| **Best** | MT3608 **EN** pin → GND | yes | < 10 µA | One wire to pin 4, if reachable |
| **Fallback** | Boost **output** (5 V → cable) | yes | ~1–3 mA | Inline in the splice, no rework |
| **Wrong** | Cell+ → module B+ | **no** | 0 | Blocks charging. Do not use. |

With the output-side switch, ~2 mA idle means a full pack self-drains in roughly
six weeks. Fine for daily use; don't store it charged for months.

An output-side switch is also what actually isolates the keyboard: the MT3608
has an inherent inductor + Schottky path from input to output, so even when it
isn't switching, its output sits at roughly battery voltage.

---

## 5. Boost calibration — do this before connecting the keyboard

The trimpot sets the boost feedback divider, which means the 5 V rail is a
mechanical setting, not a guarantee. If it is mis-set you feed overvoltage
simultaneously into the RP2040's LDO input, 46 SK6812 LEDs (absolute max ~6.0 V),
and the XIAO's VBUS. At 6 V you are marginal; at 9 V both boards are destroyed.

1. Battery in, switch on, **nothing** connected to the output.
2. Apply a dummy load — 10 Ω 5 W resistor (~500 mA) or a USB load tester.
   Setting it unloaded is worthless; boost output sags under load.
3. Adjust to **5.00 V**, measured **at the far end of the spliced cable**, not
   at the module terminals.
4. Repeat at a low battery state (~3.5 V cell) to confirm it holds regulation.
5. **Lock the pot** — nail polish or hot glue over the wiper.
6. Re-verify with the meter after any handling, every time, during bring-up.

Optional hardening: measure the pot's set resistance and replace it with a fixed
resistor. Removes the failure mode entirely; needs SMD rework.

---

## 6. Pre-power checks

Before applying power for the first time:

- [ ] Cable: continuity D+ end-to-end, D− end-to-end
- [ ] Cable: **no** continuity between VBUS and D+/D−
- [ ] Cable: no continuity VBUS ↔ GND
- [ ] Module USB-C input: ~5.1 kΩ from CC1 to GND, and CC2 to GND.
      Cheap USB-C boards often omit these — symptom is that it charges from an
      A-to-C cable but not from a C-to-C charger. Fix with two 0805 resistors.
- [ ] Battery has a PCM, rated ≥ 2 A continuous
- [ ] Boost output measured at 5.00 V under load, pot locked
- [ ] Battery is **not** connected to the XIAO's B+/B− pads

---

## 7. Power budget

At `RGB_MATRIX_MAXIMUM_BRIGHTNESS 64`, worst case (all 46 LEDs white at cap):

| | @ 5 V |
|---|---|
| RGB | ~435 mA |
| Corne logic + OLEDs | ~60 mA |
| XIAO | ~85 mA |
| **Total** | **~580 mA (2.9 W)** → ~880 mA from a 3.7 V cell |

The MT3608's "2 A" rating is *switch* current, not output current; realistic
continuous output at 3.7 V→5 V is ~1.0–1.2 A. 580 mA fits with acceptable
headroom. **At brightness cap 128 this module would not be sufficient.**

Estimated runtime on 2000 mAh (≈6.3 Wh usable):

| RGB | Draw | Runtime |
|---|---|---|
| Off | 0.66 W | ~9.5 h |
| Cap 32 (~12%) | 1.3 W | ~4.9 h |
| **Cap 64 (~25%)** | **1.9 W** | **~3.3 h** |
| Cap 128 (~50%) | 3.2 W | ~2.0 h |

### There is no sleep

The RP2040s run QMK continuously and the ESP32-S3 USB host stack polls at 1 ms.
**Idle draw equals active draw.** Walking away costs the same as typing. This is
why the switch is mandatory, and why this design gets hours where a native ZMK
board gets weeks.

### Charging

**Switch the load off, then plug in.** Cheap TP4056 boards tie BAT+ straight
through, so a load during charging prevents the TP4056 from ever seeing current
fall to its termination threshold — charge never terminates cleanly.

At 1 A, a 2000 mAh cell charges in ~2.5 h.

---

## 8. QMK-side changes

Only two, in your keymap:

```c
// config.h
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 64
```

plus an `RGB_TOG` key, and three otherwise-unused keys mapped to **F13 / F14 /
F15** for BLE profile switching (the dongle intercepts these and does not
forward them).

---

## 9. Known risks

### EMI — the Phase 2.5 go/no-go gate

foostan's README carries an active warning for v4.x:

> "There are currently reports of a bug in v4.\* caused by electromagnetic
> interference. Depending on the environment, one or both of the left and right
> keyboards may stop working... often caused by EMI emitted by mobile phones...
> move the EMI-generating device more than 30 cm away."
>
> — <https://github.com/foostan/crkbd/issues/265>

We are mounting a 2.4 GHz transmitter onto a board with a documented 2.4 GHz
susceptibility problem, and the TRRS inter-half link is the likely victim.

Mitigations: fit the **U.FL external antenna** and route it away from the PCB
and the TRRS cable; mount the pack at the outboard end of a half, away from the
TRRS jack.

**Test at the end of Phase 2, before buying any power components.** If the
halves drop out and antenna relocation doesn't fix it, the approach fails.

### Others

- **Bulk cap inrush** — start at 220 µF, not 470 µF. Charging a large output cap
  plus the Corne's own capacitance can trip the PCM or send the boost into
  hiccup. If it won't start cleanly, drop to 100 µF.
- **Thermal** — ~0.3–0.5 W dissipated in a SOT-23-6 and a small inductor inside
  a sealed pack. Touch-test after 20 min at full RGB before closing the case.
- **Inductor saturation** — cheap modules undersize the inductor. Symptoms:
  audible whine, poor efficiency, output droop. Measure the 5 V rail *under full
  RGB*; if it sags below 4.75 V, that's the cause.
- **No BIOS/UEFI access** — BLE doesn't exist pre-boot. The Corne still works
  plugged directly into a PC, since QMK is unmodified.
- **Latency** — roughly 10–25 ms added (1 ms USB poll + ≥7.5 ms BLE connection
  interval). Fine for typing, noticeable for twitch gaming.
- **Deep discharge** — a boost draws constant power, so it will run the cell down
  to the PCM trip point (2.4–3.0 V). Safe, but hard on cycle life. Phase 6 adds
  a soft warning at ~3.4 V via the BLE Battery Service.

---

## 10. Physical

Assembled pack ≈ **50 x 36 x 15 mm** — fits under one half (a Corne half is
~125 x 85 mm). Keep the spliced cable short (10–15 cm) or coil it.
