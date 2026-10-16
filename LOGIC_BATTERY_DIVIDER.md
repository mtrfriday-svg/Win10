# Logic battery divider (2 × Li-ion in parallel) – brain board GPIO1

Why: the 5V boost converter keeps outputting 5.0 V until the cells are empty, so the robot can't tell when
the logic battery is low. This divider reads the **battery terminals directly** (before the boost converter).

## Wiring (brain = ESP32-S3 N16R8)
```
 Logic pack + terminal ──┬──────────────► to the 5V boost converter IN+ (as before)
 (after the pack's       │
  protection board)     [R1 100 kΩ]
                         │
                         ├──────────┬──────────────► N16R8 GPIO1 (ADC1_CH0)
                         │          │
                        [R2 100 kΩ] [C 100 nF]      (optional: 3.3 V Zener, cathode on this node, anode to GND)
                         │          │
 Logic pack − terminal ──┴──────────┴──────────────► N16R8 GND   (same ground as the boost converter)
```
* Take the tap from the pack's **+ terminal**, in front of the boost converter (before or after the power switch;
  before = reading works with the switch off, drain is only 21 µA).
* The pack − , boost converter − and N16R8 GND are one common ground.
* 1 % resistors, ¼ W. Put the 100 nF capacitor right at the GPIO1 pin.
* The Zener (e.g. BZX55C3V3) protects the pin if a resistor ever breaks or a wire slips.

## Numbers used in the code (`main/boards/mdee-robot/config.h`)
| Name | Value |
|---|---|
| `LOGIC_BAT_R_UPPER_OHM` (R1) / `LOGIC_BAT_R_LOWER_OHM` (R2) | **100 kΩ / 100 kΩ** |
| Divider ratio (pin V ÷ battery V) | 100 ÷ 200 = **0.5** |
| Code multiplier (battery V = pin V × …) | 200 ÷ 100 = **2.0** |
| 100 % / 0 % | **4.20 V / 3.30 V** |
| Low warning (beep + blinking OLED alert) | ≤ **15 %** for 5 s |
| Critical (robot stops, ears droop, motion refused) | < **3.20 V** for 5 s |

| Battery | Pin voltage |
|---|---|
| 4.20 V (full) | 2.10 V |
| 3.70 V (nominal) | 1.85 V |
| 3.30 V (0 %) | 1.65 V |
| 3.20 V (critical) | 1.60 V |

ADC range is fine up to about 3.1 V at the pin (battery up to 6.2 V). If the divider isn't wired (or the reading is
outside 2.5-4.6 V) the OLED shows `L --.-V` and no alarms are raised.

## Where it shows
* **OLED row 2:** `M12.1V 87% L3.9V 72%` (M = motor pack, L = logic pack).
* **OLED row 3:** `!! LOGIC BATT LOW !!` (blinking) or `CHARGE NOW! STOPPED`.
* **Phone page** status line and the AI's `get_status` (`logic_v`, `logic_pct`, `logic_low`).
* The low-battery warning also plays the built-in low-battery sound once.

## Calibrate (optional, multimeter)
Measure the battery `Vbat` and the voltage at GPIO1 `Vpin`: set
`LOGIC_BAT_R_UPPER_OHM = LOGIC_BAT_R_LOWER_OHM × (Vbat / Vpin − 1)`.
