# M'DEE robot – two-board build (brain + body)

> **Start with `MDEE_COMPLETE_GUIDE.md`** – the full guide (wiring, dividers, power, voice, phone, meanings, troubleshooting).

**Not yet compiled or tested on hardware.** Expect a few small build errors on first compile.

```
 BRAIN  ESP32-S3 N16R8  (XiaoZhi firmware, folder main/boards/mdee-robot)
   Wi-Fi, AI voice, mic, speaker, ST7789 face, OLED, phone web page, MCP tools
        |  UART 115200  (3 wires: TX, RX, GND)
 BODY   ESP32-S3 Super Mini  (folder mdee_body)
   motors, 2 ultrasonics, 3 servos, NeoPixels, LED bar, battery, obstacle safety, wander/follow
```
The body keeps working safely if the brain is busy or crashes: motors stop 0.45 s after the last drive
command, and wander/follow stop 5 s after the brain goes silent.

## 1. Wiring – BRAIN (N16R8)
| Part | Pins |
|---|---|
| INMP441 mic (L/R→GND, VDD→3V3) | BCLK **4**, WS **5**, SD **6** |
| MAX98357A amp (VIN→5V) | BCLK **4**, LRC **5** (shared), DIN **7** |
| ST7789 1.69" | MOSI **11**, SCK **12**, CS **10**, DC **9**, RST **8**, BL→3V3 |
| OLED 0.96" I2C 0x3C | SDA **17**, SCL **18** |
| Link to body | TX **15** → body RX(44); RX **16** ← body TX(43); GND–GND |
| Logic-battery divider 100k/100k + 100nF (see `LOGIC_BATTERY_DIVIDER.md`) | **1** (ADC) |
| Button | BOOT (GPIO0): click = talk, double-click = emergency stop |

## 2. Wiring – BODY (Super Mini)  (`mdee_body/body_config.h`)
| Part | Pins |
|---|---|
| TB6612 | STBY **4** (+10k pulldown to GND), AIN1 **5**, AIN2 **6**, PWMA **7**, BIN1 **8**, BIN2 **9**, PWMB **10** |
| HC-SR04 ×2 | TRIG **11** (shared), ECHO front **12**, ECHO rear **3** – each echo via 1k/2k divider |
| SG90 neck turn / neck tilt / ears | **13** / **15** / **16** (own 5V rail) |
| NeoPixel ×2 | DIN **2** (330Ω series) |
| Battery divider (100k top / 22k bottom, 100nF to GND) | **1**  (details: BATTERY_WIRING.md) |
| PCF8574 (8 white LEDs, active low) | SDA **17**, SCL **18** |
| Link to brain | TX label **43** → brain RX(16); RX label **44** ← brain TX(15) |

Super Mini boards differ. Confirm that your board exposes these GPIOs; edit `body_config.h` if not (avoid 0, 19, 20, 26-32, 45, 46).
Logs on the body go to the USB port (UART0 pins are used for the link). 8 LEDs: 3V3 → 330Ω → LED → PCF8574 P0..P7; P0-P3 left, P4-P7 right.

## 3. Power
12V pack → fuse → TB6612 VM + motors; buck → 5V for both ESP boards, amp, HC-SR04, NeoPixels;
**separate 5V buck for servos**. All grounds joined (including the link GND). 470µF on VM, 1000µF on servo rail.
Motors must be 12V-rated. One TB6612 channel = 1.2A continuous; two motors per channel only if small.

## 4. Flash
Body (ESP-IDF 6.x, Super Mini over USB-C; hold BOOT while plugging if it isn't detected):
```
cd mdee_body
idf.py set-target esp32s3
idf.py -p COMx flash monitor
```
Brain:
```
cd ..            # Bot2-main
python scripts/build.py mdee-robot
idf.py -p COMy flash monitor
```
First boot: robot opens a Wi-Fi hotspot → join, enter home Wi-Fi, then activate on xiaozhi.me (free STT + LLM + TTS).

## 5. Controls
* **Voice:** "move forward fast", "turn left slowly", "stop", "wander", "follow me", "look up", "dance", "battery?", "what's in front of you?".
* **Phone:** `http://<robot-ip>/` (IP shown on the OLED). Joystick, slow/medium/fast/turbo, modes, head, lights.
* **Speeds:** slow 35 / medium 55 / fast 75 / turbo 95 % (set in BOTH config files).
* **Safety (body):** stop at 18 cm, slow under 40 cm (front when forward, rear when reversing), ramped acceleration, battery cutoff.
* OLED says `NO BODY LINK` if the two boards aren't talking (check TX/RX crossing and GND).

## 6. Tuning
Wheels reversed → `MOTOR_*_INVERT`. Face rotated → `DISPLAY_*` in brain `config.h`. Battery % wrong → divider resistor values in `body_config.h`.

## 7. Limits
Follow = nearest object in front (no camera). BLE not used (BLE is taken by Wi-Fi provisioning); control is via the Wi-Fi web page.

## 8. Building on GitHub from a phone (Termux)
```
pkg install git unzip
unzip Bot2-mdee.zip && cd Bot2-main
git init && git add . && git commit -m "M'DEE"
git branch -M main
git remote add origin https://<YOUR_TOKEN>@github.com/<USER>/<REPO>.git
git push -u origin main
```
Use a GitHub personal access token (Settings → Developer settings) with "repo" + "workflow" scope.
Workflow `.github/workflows/mdee.yml` builds both firmwares on every push. Open the repo → **Actions** → latest run →
download `mdee_brain_n16r8_*` and `mdee_body_supermini_*` (each contains `merged-binary.bin`, flash at address **0x0**).
The stock all-boards workflow is switched to manual-only so it doesn't waste minutes.

## 7b. Repo contents
This repo was trimmed to your robot only: `main/` (XiaoZhi firmware + board `mdee-robot`), `mdee_body/` (Super Mini firmware), `scripts/` (3 build scripts), `partitions/`, `.github/workflows/mdee.yml`. All other boards, docs and READMEs were removed, so GitHub builds only your two firmwares.
Docs to read: `AUDIO_COMMANDS.md` (internet audio), `MDEE_README.md`, `AUDIO_URL_COMMANDS.md` (internet audio), `DESIGN_NOTES.md`, `BATTERY_DIVIDER.md` (motor pack), `LOGIC_BATTERY_DIVIDER.md` (logic pack).

## 8. Build on GitHub from a phone (Termux)
See **`TERMUX_GUIDE.md`** (step by step, with `termux_push.sh` that guarantees the hidden `.github` workflow is pushed).
The boot logo is `main/boards/mdee-robot/boot_logo.h` (GIF bytes) – replace it to change the animation.
