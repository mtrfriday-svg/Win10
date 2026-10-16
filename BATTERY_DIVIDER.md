# Battery voltage divider (12 V pack, 3S Li-ion) – body board GPIO1

Reads the **motor pack** (3 × 3.7 V cells in series: 9.0-12.6 V). The logic pack (2 cells parallel → 5 V boost) is not measured.

## Wiring
```
 Pack + (after fuse/switch) ──[ R1 = 100 kΩ ]──┬──────────── Super Mini GPIO1 (ADC)
                                               │
                                               ├──[ R2 = 22 kΩ ]──┐
                                               ├──[ C = 100 nF ]──┤
                                               └──[ Zener 3.3 V ]─┤   (optional safety, cathode to the node)
                                                                  │
 Pack − ──────────────────────────────────────────────────────────┴──── Super Mini GND
```
* **The pack − and the Super Mini GND must be the same ground** (join the motor pack − with the logic pack −). If not, readings are wrong.
* R1/R2: 1 % metal-film if possible. C: 100 nF ceramic right at the GPIO1 pin (steadies the ADC against motor noise).
* The Zener (e.g. BZX55C3V3) is cheap insurance: if R2 ever breaks or a wire comes loose, the pin would see the full 12.6 V and the ESP32 would die. With it, the pin is clamped to 3.3 V.

## Ratio used in the code (`mdee_body/body_config.h`)
```
BATTERY_R_UPPER_OHM = 100000   (R1)
BATTERY_R_LOWER_OHM = 22000    (R2)

Vpin = Vbat × R2 / (R1 + R2) = Vbat × 22 / 122 = Vbat × 0.1803
Vbat = Vpin × (R1 + R2) / R2 = Vpin × 5.545
```
| Pack | Voltage | At the pin |
|---|---|---|
| Full (4.2 V/cell) | 12.6 V | 2.27 V |
| Nominal | 11.1 V | 2.00 V |
| Empty (code's 0 %: 3.2 V/cell) | 9.6 V | 1.73 V |
| Soft cutoff (motors off after 5 s) | < 9.3 V | 1.68 V |
| Charger overshoot | 13.0 V | 2.34 V |

The ADC is set to 12 dB attenuation (good up to about 3.1 V at the pin = 17 V pack), so there is plenty of margin. Battery % is a straight line between 9.6 V (0 %) and 12.6 V (100 %); the divider draws only 0.1 mA. Below 4 V at the pin-scaled reading, the robot assumes "USB only, no battery" and hides the battery level.

## Using other resistor values
Put your real values in `BATTERY_R_UPPER_OHM` and `BATTERY_R_LOWER_OHM`. Keep the pin voltage at full pack (12.6 V) between about 1.5 V and 2.8 V, e.g. 47k/10k (ratio 0.175) works too.

## Calibrate (2 minutes, multimeter)
Measure the pack voltage `Vbat` and the voltage at GPIO1 `Vpin`. Then set
`BATTERY_R_UPPER_OHM = BATTERY_R_LOWER_OHM × (Vbat / Vpin − 1)`. This absorbs resistor tolerance.

## Notes for your two packs
* Use a 3S BMS (with balancing) on the 12 V pack; the code's cutoff is only a soft backup.
* Logic: 2 cells parallel (3.0-4.2 V) → 5 V boost: choose a ≥ 3 A boost module and put 1000 µF on its output, since the Wi-Fi brain, amp and servos share it. Keep servos on their own boost/buck if you can.
* Want the logic pack voltage on the OLED too? Divider 100k/100k to the brain's GPIO1 (ratio 0.5, max 2.1 V) – tell me and I'll add the code.
