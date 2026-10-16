# M'DEE robot – the complete guide

> **Honest status:** all code was written and checked by reading it. It has **never been compiled or run on real hardware yet**.
> Expect a few small build errors on the first GitHub build and some tuning (motor direction, screen orientation, servo angles).
> Send me the first error lines and I will fix them.

---

## 0. Contents
1. What the robot is
2. FM radio vs internet radio vs audio URL (please read)
3. Parts list (what you have, what you still need)
4. Wiring – brain, body, link, power
5. Voltage dividers (motor pack, logic pack, ultrasonic echo)
6. Power notes and battery protection
7. Build the firmware on GitHub from your phone
8. First start-up
9. Using the robot: voice, phone page, buttons
10. Everything the robot does and what it means (face, ears, lights, OLED, sounds, movements)
11. Autonomy (wander / follow) and safety
12. Internet audio in detail
13. Settings you can change
14. Troubleshooting
15. Limits and ideas for later
16. File map and other documents
17. Quick reference card

---

## 1. What the robot is
A pet-style robot: it talks and answers real questions using an AI, shows an animated face, moves its ears and neck, drives on 4 wheels,
avoids obstacles, wanders or follows things, shows lights, plays internet audio, and can be driven from your phone.

It uses **two boards** that talk over 3 wires:

```
 BRAIN  ESP32-S3 N16R8  ("XiaoZhi" AI firmware + our board "mdee-robot")
   microphone, speaker, face screen, OLED, Wi-Fi, AI voice, phone web page, internet audio, logic-battery meter
        |  UART 115200 (TX, RX, GND)
 BODY   ESP32-S3 Super Mini  (our own firmware in folder mdee_body)
   4 motors (TB6612), 2 distance sensors, 3 servos, 2 NeoPixels, 8 white LEDs, motor-battery meter, obstacle safety,
   wander/follow, ear behaviour
```
Why two boards: there were not enough pins on one. Bonus: the body keeps the robot safe even if the brain is busy or crashes.

The AI part (hearing you, understanding, answering, speaking) runs on the **XiaoZhi cloud service (xiaozhi.me)**, not inside the robot.
So the robot needs **Wi-Fi and internet** for talking. Driving from the phone page and the safety/autonomy work without internet
(the phone and the robot must be on the same Wi-Fi).

---

## 2. FM radio vs internet radio vs audio URL (please read)
**There is NO FM radio in this project.** I did not build one. Real FM radio needs extra hardware (an FM receiver chip such as RDA5807M
plus an antenna) that you do not have, and extra code that does not exist yet. Radio in the air (88-108 MHz) works without internet; that is
what "FM" means. It is **not** what the robot does.

What the robot **does** have is **internet audio ("audio URL")**. In plain words:
1. You give the robot a **link** (URL) to an **MP3 file** or to an **internet-radio stream** (a never-ending MP3 stream a radio station puts on the internet).
2. The robot connects to that link through your **Wi-Fi**, downloads the audio a little at a time, turns the MP3 into sound, and plays it on its speaker.
3. It needs **internet** the whole time. If the Wi-Fi drops, the sound stops.
4. "Internet radio" = a station's MP3 stream link. Radio directories (for example radio-browser.info) list direct stream links; choose ones that are **MP3**.

What works / does not work:
| Works | Does not work |
|---|---|
| Direct link ending in `.mp3` | YouTube, Spotify, SoundCloud, Apple Music **web pages** |
| Internet-radio stream that is MP3 ("audio/mpeg") | AAC, OGG, FLAC, WAV files |
| `http://` and `https://` links | Playlist files (`.m3u`, `.pls`), links that need a login |

If you really want real FM later: add an RDA5807M FM module, a wire antenna, and an analog amplifier (e.g. PAM8403), and ask me to write the code. It is a separate project.

---

## 3. Parts list
### You already have
| Part | Used for |
|---|---|
| ESP32-S3 **N16R8** (WROOM-1) | Brain |
| ESP32-S3 **Super Mini** | Body controller |
| 4 × TT gear motor (12 V type) | Wheels |
| TB6612FNG | Motor driver |
| 3 × SG90 servo | Neck turn, neck tilt, ears |
| 2 × HC-SR04 | Front and rear distance sensors |
| 2 × INMP441 mic (1 used) | Hearing |
| 2 × MAX98357A (1 used) | Speaker amplifier |
| ST7789 1.69" 240×280 | Face |
| 0.96" I2C OLED | Status screen |
| 2 × NeoPixel (WS2812) | Colour status lights |
| 8 × standard LED + resistors | Light bar |
| 3S Li-ion pack (3 cells in series, 12 V) | Motors |
| 2 × Li-ion in parallel + 5 V boost | Logic (brains, sensors, lights) |
| Resistors for dividers | Battery meters |

### You still need (cheap but required)
| Part | Why |
|---|---|
| **PCF8574** I2C module (address 0x20) | Drives the 8 white LEDs without using pins |
| **Speaker** 4-8 Ω, 3 W | Sound |
| **5 V buck converter, ≥ 3 A** (12 V → 5 V) | Separate supply for the **servos** |
| **5 V boost converter, ≥ 3 A** | Logic supply (you have one; check it is ≥ 3 A) |
| **3S BMS** (with balancing) for the 12 V pack, and a **protection board** for the 2-cell pack | Prevents over-discharge/short circuit |
| **Fuse** (e.g. 5 A) on each pack + power switch | Safety |
| **Capacitors**: 470 µF on motor power, 1000 µF on the servo rail, 1000 µF on the logic 5 V, 100 nF at each battery-meter pin | Stops resets and noise |
| 1 kΩ + 2 kΩ resistors ×2 | Echo dividers for the two HC-SR04 |
| 330 Ω resistors | NeoPixel data line (1) and LEDs (8) |
| 10 kΩ resistor | STBY pull-down on the TB6612 |
| 3.3 V Zener diodes ×2 (BZX55C3V3) – optional | Protect the two battery-meter pins |
| Wires, headers, 3 mm light pipes/diffusers for the NeoPixels | Build |
| Optional: ear linkage parts (rod + arms) | Move both ears with one servo |

---

## 4. Wiring
**Golden rules**
* Every ground is joined: both packs −, boost, buck, TB6612, both ESP boards, all sensors.
* Never connect 5 V to an ESP32 GPIO. All signals into the ESP boards must be 3.3 V.
* Motors and servos get their own power; do not power them from the ESP boards' 3.3 V pin.
* Logic parts at **3.3 V**: INMP441, OLED, ST7789, PCF8574, TB6612 logic (VCC). At **5 V**: MAX98357A, HC-SR04, NeoPixels, servos.

### 4.1 Brain (ESP32-S3 N16R8) – pins
| Part | Part pin | Brain GPIO |
|---|---|---|
| INMP441 mic | SCK | **4** |
| | WS | **5** |
| | SD | **6** |
| | L/R, GND | GND |
| | VDD | 3.3 V |
| MAX98357A amp | BCLK | **4** (shared with mic) |
| | LRC | **5** (shared with mic) |
| | DIN | **7** |
| | VIN | 5 V |
| | GND | GND |
| | speaker | to + and − |
| ST7789 face | SCL/CLK | **12** |
| | SDA/MOSI | **11** |
| | CS | **10** |
| | DC | **9** |
| | RES | **8** |
| | BLK (backlight) | 3.3 V |
| | VCC, GND | 3.3 V, GND |
| OLED 0.96" | SDA | **17** |
| | SCL | **18** |
| | VCC, GND | 3.3 V, GND (address 0x3C) |
| Link to body | brain TX | **15** → body RX label (GPIO44) |
| | brain RX | **16** ← body TX label (GPIO43) |
| Logic-battery meter | divider middle | **1** (see section 5) |
| BOOT button | built-in | GPIO0 |

Do not use GPIO 26-37 on the N16R8 (flash/PSRAM) and avoid 0, 19, 20, 45, 46.

### 4.2 Body (ESP32-S3 Super Mini) – pins
(Super Mini boards differ slightly: check your board has these pins; if not, change `mdee_body/body_config.h`.)
| Part | Part pin | Body GPIO |
|---|---|---|
| TB6612 | STBY | **4** (and 10 kΩ from STBY to GND) |
| | AIN1 / AIN2 / PWMA | **5 / 6 / 7** |
| | BIN1 / BIN2 / PWMB | **8 / 9 / 10** |
| | VCC (logic) | 3.3 V |
| | VM | 12 V motor pack (after fuse and switch) |
| | A01/A02 | the two **left** motors (in parallel) |
| | B01/B02 | the two **right** motors (in parallel) |
| HC-SR04 (both) | VCC / GND | 5 V / GND |
| | TRIG (both joined) | **11** |
| | front ECHO | **12** via divider (4.3) |
| | rear ECHO | **3** via divider |
| Servo neck **turn** | signal | **13** |
| Servo neck **tilt** | signal | **15** |
| Servo **ears** | signal | **16** |
| (all servos) | power | separate 5 V buck, + 1000 µF |
| NeoPixel ×2 (chained) | DIN | **2** via 330 Ω |
| | VCC / GND | 5 V / GND |
| PCF8574 | SDA | **17** |
| | SCL | **18** |
| | VCC, GND, A0-A2 | 3.3 V, GND, GND (address 0x20) |
| Motor-battery meter | divider middle | **1** (see section 5) |
| Link to brain | body TX label (GPIO43) | → brain GPIO16 |
| | body RX label (GPIO44) | ← brain GPIO15 |

Test the motors: if a wheel side drives backwards when told "forward", change `MOTOR_LEFT_INVERT` or `MOTOR_RIGHT_INVERT` to `true` in `mdee_body/body_config.h`.
One TB6612 channel gives 1.2 A continuous (about 3 A peak). Two small motors per side are fine; if they stall hard, use a second driver.

### 4.3 Ultrasonic ECHO dividers (important)
The HC-SR04 echo pin outputs **5 V**, but the ESP32 accepts only 3.3 V.
```
 ECHO ──[1 kΩ]──┬── ESP32 GPIO (12 or 3)
                │
              [2 kΩ]
                │
               GND
```
Result: 5 V × 2/3 = 3.33 V. Do this for both sensors.

### 4.4 LEDs on the PCF8574 (8 white LEDs)
```
 3.3 V ──[330 Ω]──►|── (LED, anode to +)  cathode ──► PCF8574 pin P0..P7
```
The LED turns **on** when the PCF8574 pin goes low. **P0–P3 = left side**, **P4–P7 = right side** (turn signals use these halves).

### 4.5 Power layout
```
 12 V pack (3 × Li-ion in series + 3S BMS)
      │ fuse + switch
      ├──────────────► TB6612 VM and the 4 motors (+470 µF)
      ├──[100k/22k divider]──► body GPIO1  (motor battery meter)
      └──► 5 V buck (≥3 A) ──► servos only (+1000 µF)

 Logic pack (2 × Li-ion in parallel + protection)
      │ switch
      ├──[100k/100k divider]──► brain GPIO1  (logic battery meter, taken at the battery terminals)
      └──► 5 V boost (≥3 A) ──► brain, body, amp, HC-SR04 ×2, NeoPixels
                                 (+1000 µF; the boards make their own 3.3 V)

 ALL grounds (both packs −, boost, buck, TB6612, both boards) joined together.
```
Typical logic draw is about 0.5-1.5 A at 5 V (Wi-Fi + amp + LCD + lights), which is why a ≥ 3 A boost module is advised.

---

## 5. Voltage dividers
### 5.1 Motor pack (12 V, 3S) → body GPIO1
```
 Pack + (after fuse) ──[R1 100 kΩ]──┬──────► body GPIO1
                                    ├──[R2 22 kΩ]──► GND
                                    ├──[100 nF]────► GND
                                    └──[3.3 V Zener, optional]──► GND (cathode on the node)
```
* Ratio = 22 ÷ 122 = **0.1803**. The code multiplies the pin voltage by **5.545**.
* Constants: `BATTERY_R_UPPER_OHM = 100000`, `BATTERY_R_LOWER_OHM = 22000` in `mdee_body/body_config.h`.
* 0 % = 9.6 V, 100 % = 12.6 V. Motors cut off if below about 9.3 V for 5 seconds.

| Pack | At the pin |
|---|---|
| 12.6 V (full) | 2.27 V |
| 11.1 V (nominal) | 2.00 V |
| 9.6 V (0 %) | 1.73 V |
| 13.0 V (charger overshoot) | 2.34 V |

The ADC reads safely up to about 3.1 V at the pin (a 17 V pack), so there is a large margin.

### 5.2 Logic pack (2 cells in parallel, 3.0-4.2 V) → brain GPIO1
```
 Battery + terminal ──[R1 100 kΩ]──┬──────► brain GPIO1
 (tap in front of the boost)       ├──[R2 100 kΩ]──► GND
                                   ├──[100 nF]──────► GND
                                   └──[3.3 V Zener, optional]──► GND
```
* Ratio = **0.5**; the code multiplies the pin voltage by **2.0**.
* Constants in `main/boards/mdee-robot/config.h`: `LOGIC_BAT_R_UPPER_OHM`, `LOGIC_BAT_R_LOWER_OHM`.
* 100 % = 4.20 V, 0 % = 3.30 V. Low warning ≤ 15 % for 5 s. Critical below 3.20 V for 5 s.

| Battery | At the pin |
|---|---|
| 4.20 V | 2.10 V |
| 3.70 V | 1.85 V |
| 3.30 V (0 %) | 1.65 V |
| 3.20 V (critical) | 1.60 V |

The reason for this meter: the boost converter always outputs 5 V until the cells are nearly empty, so the robot could not otherwise know.
If the divider is not wired, the OLED shows `L --.-V` and raises no alarm.

### 5.3 Calibrating (2 minutes, multimeter)
Measure the battery voltage `Vbat` and the voltage at the pin `Vpin`. Then set
`R_UPPER = R_LOWER × (Vbat ÷ Vpin − 1)` in the matching constant. This cancels resistor tolerance.

### 5.4 Safety of the dividers
If the lower resistor or a wire breaks, the pin would see the full battery voltage and could be destroyed. The optional 3.3 V Zener
on each pin prevents that. Use 1 % resistors if you can.

---

## 6. Power notes and battery protection
* **Never** connect the 12 V pack to a logic pin or the 5 V rail. Check polarity twice.
* Put a **fuse** and a **switch** on each pack. Use a **3S BMS** on the 12 V pack. Use a **protection board** on the 2-cell pack. Li-ion cells can catch fire if shorted or over-discharged.
* Charge with a charger made for your pack (3S balance charger; a 1S/2P Li-ion charger). Do not charge unattended.
* Switch on order: any order is fine, because the body stops motors until told to move. Switch **off** the motor pack when you work on the wiring.
* Battery alarms:

| Level | What the robot does |
|---|---|
| Logic pack ≤ 15 % (5 s) | Plays the low-battery sound once, OLED row shows blinking `!! LOGIC BATT LOW !!` |
| Logic pack < 3.2 V (5 s) | **Critical:** robot stops, ears droop, OLED shows `CHARGE NOW! STOPPED`, refuses driving and autonomy |
| Motor pack < 9.3 V (5 s) | Body cuts the motors (phone page shows "LOW – motors off"); talking still works |
| Ears droop while idle | Motor pack below about 10 % |

* USB-only testing: if the motor pack is not connected the OLED shows `M --.-V` and no motor-battery alarm is raised. The motors will not move without the 12 V pack.

---

## 7. Build the firmware on GitHub from your phone
The repo contains only your robot (two firmwares). GitHub builds both automatically.

Short version (full version with error table: **TERMUX_GUIDE.md**):
```
pkg update -y && pkg upgrade -y
pkg install -y git unzip nano curl
termux-setup-storage
cd ~
cp ~/storage/downloads/Bot2-mdee.zip .
unzip -o Bot2-mdee.zip
cd Bot2-main
ls -a
bash termux_push.sh "first upload"
```
Before that: make an empty GitHub repo and a **token** (classic, tick **repo** and **workflow**). When Git asks for a password, paste the token.
The script re-creates the hidden `.github/workflows/mdee.yml` and prints `OK` only when the workflow really is in the commit.

Then: GitHub → your repo → **Actions** → *Build M'DEE robot* → latest run → **Artifacts**:
* `mdee_brain_n16r8_…` → flash on the **N16R8**
* `mdee_body_supermini_…` → flash on the **Super Mini**

Each is a zip holding `merged-binary.bin`: a complete image to write at address **0x0**.

**Flashing without a PC:** I have not verified a phone-only flashing method. Options: an Android flasher app or browser tool that supports ESP32-S3 over a USB-OTG cable, or borrow a PC once and use any ESP flash tool (for example the "ESP Web Flasher" in Chrome, or `esptool.py write_flash 0x0 merged-binary.bin`).
Super Mini: hold BOOT while plugging in USB if it is not detected.

**Changing something later:** edit a file in Termux (`nano path/to/file`), then `bash termux_push.sh "what I changed"`, then download the new artifacts and flash again.

---

## 8. First start-up
1. Switch on both packs. The brain's face screen plays your **M'DEE boot logo animation** (about 4 seconds), then the animated face appears.
2. The OLED shows state text. **Wi-Fi setup (first time only):** the robot opens a hotspot named **Xiaozhi-XXXX**. Join it with your phone and open the address shown on the OLED (usually `192.168.4.1`), choose your home Wi-Fi and enter the password. The robot restarts and joins.
   To redo Wi-Fi later: click BOOT **while the robot is starting up** (right after power-on).
3. **Activation:** on first connection the robot gives a **6-digit code** (shown on the OLED and spoken). Go to **xiaozhi.me** on your phone browser, log in, add a device and enter the code. There you can also choose the AI model, voice and add extra tools.
4. Check the OLED: row 0 should show Wi-Fi bars and the mode, **not** `NO BODY LINK`. If it says `NO BODY LINK` see Troubleshooting.
5. Open the phone page: `http://<robot-ip>/` (the IP is shown on the OLED while idle, under "Phone control page"). Phone and robot must be on the same Wi-Fi.
6. Test with the robot lifted off the ground first: tap the joystick slowly and check the wheels spin the right way.

---

## 9. Using the robot
### 9.1 Talking (voice)
* **Say the wake word.** The firmware in this build uses the model `wn9_hijason_tts2` (meant to be "Hi, Jason"). If it does not react, **click the BOOT button** to start talking instead.
  You can change the wake word by changing `--wake-word` in `.github/workflows/mdee.yml` to another ESP-SR model name such as `wn9_hiesp`.
* Flow: say the wake word → ears perk up, NeoPixels **blue** (listening) → speak → robot answers with the mouth moving and NeoPixels **green pulse** → back to standby (NeoPixels **cyan pulse**). Purple pulse appears only while the robot is connecting to the server.
* Ask anything: questions, jokes, maths, news-style questions (depends on the AI model you choose on xiaozhi.me), "turn the volume up", "set the volume to 60".
* What you said and the robot's answer appear on the **OLED** (letters only; other alphabets show `?`).

### 9.2 Voice commands for the robot's body
The AI understands normal sentences and calls these tools for you:
| Say (examples) | What happens |
|---|---|
| "Move forward", "go forward fast", "back up slowly" | Drives. Speeds: **slow 35 %, medium 55 %, fast 75 %, turbo (high speed) 95 %** |
| "Turn left", "spin right", "turn around" | Turns on the spot (about 0.5-0.7 s per quarter turn at medium; there is no gyro, so angles are approximate) |
| "Go forward for 3 seconds" | Duration from 0.1 to 10 s (default 1.5 s) |
| "Stop", "freeze", "halt" | Stops at once and goes to manual mode |
| "Wander around", "explore" | Autonomous roaming with obstacle avoidance |
| "Follow me" (a hand or person in front) | Follow mode (see section 11) |
| "Stay", "manual mode" | Autonomy off |
| "Look left/right/up/down", "look at me" | Neck turn/tilt (angles 0-180, 90 = straight) |
| "Wiggle your ears", "ears up", "ears down", "flick your ears" | Ear gestures |
| "Turn on the lights", "chase lights", "blink", "left signal", "battery lights" | White LED bar patterns |
| "Make your eyes red", "rainbow lights", "breathing blue light" | NeoPixel colours/effects |
| "Dance" | About 4 s: spinning left/right, head moves, ear wiggle, rainbow lights |
| "How is your battery?", "What is in front of you?", "Are you ok?" | Reads status (both batteries, distances, mode, wheel speeds) |
| "Play lofi", "stop the music", "what favourites do you have?" | Internet audio (section 12) |
| "Set volume to 50" | Speaker volume (built-in XiaoZhi tool) |

The AI sometimes needs a repeat if it did not understand. Speak clearly, near the microphone.

### 9.3 Buttons
| Action on BOOT (GPIO0) button | Result |
|---|---|
| Single click while music plays | Stops the music |
| Single click otherwise | Starts/stops talking (same as the wake word) |
| Single click during start-up | Enters Wi-Fi setup mode |
| **Double click** | **Emergency stop** (drives stop, manual mode) |

### 9.4 The phone page
Open `http://<robot-ip>/` on the same Wi-Fi. Optional PIN: add `#define WEB_CONTROL_PIN "1234"` in `config.h`, then open `http://<ip>/?k=1234`.
| Control | What it does |
|---|---|
| Status box | Mode, motor pack V/%, logic pack V/%, front/rear distance in cm, LOW warnings |
| **Slow / Medium / Fast / Turbo** | Sets how strong the joystick drives (35/55/75/95 %) |
| **Joystick** | Hold and drag to drive and steer. **Lifting your finger stops the robot.** The robot also stops by itself 0.45 s after the last command (lost Wi-Fi, locked phone) |
| **STOP** | Stops immediately |
| **Manual / Wander / Follow** | Autonomy mode |
| Neck turn / Neck tilt sliders | Neck servos (0-180) |
| **Ears up / Ears down / Wiggle / Auto** | Ear gestures (Auto = natural idle flicks) |
| Light buttons: Off / On / Chase / Blink / Battery | White LED bar |
| Rainbow / Breathe / Red / Green / Blue | NeoPixels |
| **Play internet audio** box + Play / Stop | Paste an MP3 link and play it. Favourite names appear as buttons |

Anything you do manually (joystick, STOP) cancels autonomy modes.

---

## 10. Everything the robot does and what it means
### 10.1 Face (ST7789)
| What you see | Meaning |
|---|---|
| Boot logo animation (~4 s) | Starting up |
| Blinking eyes, eyes glancing around | Awake and idle (blink every 2-6 s) |
| Mouth moving | The robot is speaking (mouth moves only while it speaks) |
| **Neutral** (cyan eyes, soft smile) | Normal |
| **Happy / laughing / funny / winking / silly** | Bigger smile; laughing narrows the eyes and opens the mouth; winking closes one eye |
| **Loving / kissy / delicious** | Pink eyes, big smile |
| **Embarrassed** | Pink eyes, small smile, looks away |
| **Cool** | Green eyes, half-closed lids |
| **Sad / crying** | Blue eyes, drooping lids, frown (crying: small open mouth) |
| **Angry** | Red eyes, slanted lids, flat mouth |
| **Surprised / shocked** | White eyes, round open mouth |
| **Thinking** | Purple eyes looking up and aside |
| **Confused** | Orange-yellow eyes, tilted lids, small frown |
| **Sleepy** | Dim blue eyes, almost closed lids |

The AI chooses the emotion while it talks.

### 10.2 Ears (one servo moves both)
| Ear action | Meaning / when |
|---|---|
| Ears **perk up** | Listening to you; also surprised / thinking / confused |
| Ears **wiggle** | Happy, laughing, loving, funny, silly, winking, dancing |
| Ears **droop back** | Sad, crying, sleepy, angry, embarrassed; logic battery critical; motor battery below ~10 % |
| Occasional small **flick** (every 5-12 s) | Idle "pet" life |
| Relaxed in the middle | Normal / waiting |

### 10.3 NeoPixels (2 colour lights)
| Colour | Meaning |
|---|---|
| **Blue**, steady | Listening to you |
| **Purple**, pulsing | Connecting to the server |
| **Green**, pulsing | Speaking |
| **Cyan**, pulsing | Standby (idle) |
| **Rainbow** | Dancing, or playing internet audio |
| Any colour you chose | Set by voice or phone page (until the next state change) |

### 10.4 The 8 white LEDs
| Pattern | Meaning |
|---|---|
| Off | Nothing |
| On | All lit |
| **Chase** | Running light bouncing left-right (fun/dance) |
| **Blink** | All blink |
| **Left / Right** | Turn signal: left four or right four blink |
| **Battery** | Bar of lit LEDs = motor-pack charge (8 lit = full) |

### 10.5 The OLED (128 × 64), rows from the top
| Row | Content |
|---|---|
| 1 | `WiFi ||||` (bars = signal strength) and the mode (`manual`, `wander`, `follow`). Shows `NO BODY LINK` if the body board is not answering |
| 2 | `M12.1V 87% L3.9V 72%` – **M** = motor pack, **L** = logic pack. `--.-V` means not connected/not measured |
| 3 | Distances `Front 123  Rear 45` in cm (`--` = nothing seen / too far). Replaced by a blinking `!! LOGIC BATT LOW !!` or `CHARGE NOW! STOPPED` when the logic battery is low/critical |
| 4 | Assistant state (Listening, Speaking, Standby, Connecting, ..., `Loading:`/`Playing:` for audio, error messages like `Audio: cannot open URL`) |
| 5-8 | What you said (`You: ...`) and the answer (`M'DEE: ...`). When idle: "Phone control page: http://IP" |

### 10.6 Sounds
The robot speaks answers, plays the system prompts (Wi-Fi/activation messages), a one-time **low-battery sound** for the logic pack, and your internet audio.
There are no beeps for obstacles.

### 10.7 Movement and what it means
| Movement | Meaning |
|---|---|
| Soft start/stop | Acceleration is ramped (about 0.25 s) to protect gears |
| Slows down | Something is within 40 cm in the direction of travel |
| Stops by itself | Something is within 18 cm in the direction of travel (front when going forward, rear when reversing) |
| Spins on the spot | Turn command; allowed even near obstacles, so keep space around the body |
| Stops after ~0.45 s with no commands | Safety timer (phone lost, finger lifted) |
| Head fidgets a little when idle | Natural idle life (turn ±30°, tilt ±10° every 7-16 s) |
| Won't move, ears droop | Logic battery critical |

---

## 11. Autonomy and safety
**Wander** (medium speed): drives forward. When something is closer than about 24 cm, it backs up if the rear is clear (about 0.45 s, longer if stuck 3 times in 6 s), then turns a random way for about 0.45-1.1 s and goes on. It uses the front and rear distance sensors only (no camera), so it can miss thin things (chair legs, cables) or soft/angled surfaces that do not reflect ultrasound.

**Follow:** keeps about 30 cm from whatever is in front. Moves forward if the object is 40-90 cm away, reverses slowly if closer than 22 cm, stands still in between or if nothing is within 90 cm. It follows "the nearest thing ahead", not a specific person.

**Safety layers**
1. Body firmware stops forward driving at 18 cm and reverse driving at 18 cm (rear sensor).
2. Motors have a deadman timer: no new command for 0.45 s = stop.
3. If the brain goes silent for 5 s while autonomy is on, the body drops to manual and stops.
4. Low motor battery cuts the motors; critical logic battery stops the robot.
5. STBY is held low by a resistor, so motors are off while the body boots.
6. Double-click BOOT or tap STOP at any time.

---

## 12. Internet audio in detail
(See also **AUDIO_COMMANDS.md**.)

**Ways to start it**
1. **Phone page:** open the robot page, paste the link in the audio box, tap **Play**. Tap **Stop** to end.
2. **Favourites:** names stored in the firmware (`main/boards/mdee-robot/favorites.h`). Say "play lofi" (or tap the favourite button).
3. **Voice with a link:** "Play this link ..." only works if the AI already has the link text (for example you typed it in the chat). Long links cannot be dictated reliably.

**What happens:** `Loading: name` on the OLED → robot connects → `Playing: name`. While playing the NeoPixels run rainbow and the ears wiggle now and then.

**Stopping:** Stop button on the page, a single click of BOOT, saying "stop the music", or the wake word (the music always yields to the assistant).
The robot has **no echo cancellation**: loud music can drown out the wake word. Use the button or lower the volume.

**Waiting rule:** audio does not start while the robot is talking or listening. It waits up to 30 s for it to become idle.

**Memory:** about 50 KB while playing; if memory is low it says `not enough free memory` instead of crashing.

**Adding favourites (Termux):**
```
cd ~/Bot2-main
nano main/boards/mdee-robot/favorites.h
```
Add `{"lofi radio", "https://your.stream/lofi.mp3"},`, save (CTRL+O, Enter), exit (CTRL+X), then `bash termux_push.sh "new favourites"`, download the new brain artifact and flash it.
Links typed on the phone page never need a rebuild.

**Test a link before using it:**
```
curl -sIL "https://your.link/song.mp3" | grep -i -E "^HTTP|content-type"
```
You want `200` and `audio/mpeg`.

**Phone-only trick for your own MP3:** put the file in your GitHub repo and use
`https://raw.githubusercontent.com/<user>/<repo>/main/<file>.mp3` (only host music you have the right to share).

**Messages**
| Message | Meaning / fix |
|---|---|
| `Audio: cannot open URL` | Wrong link, no internet, or the site blocks devices |
| `Audio: HTTP error 403/404/301/302` | Not a direct link; use the final file/stream address |
| `Audio: not an MP3 (MP3 links only)` | It is a web page, AAC, OGG, FLAC or WAV |
| `Audio: connection lost` | Wi-Fi dropped or the stream ended |
| `not enough free memory` | Try again in a few seconds |
| Stutters | Weak Wi-Fi or very high bitrate; use a 64-128 kbps stream |

---

## 13. Settings you can change
| Setting | File | Default |
|---|---|---|
| Speed levels slow/medium/fast/turbo | `config.h` (brain) **and** `body_config.h` (body, used by wander/follow) | 35 / 55 / 75 / 95 % |
| Stop distance / slow-down distance | `mdee_body/body_config.h` `OBSTACLE_STOP_CM`, `OBSTACLE_SLOW_CM` | 18 / 40 cm |
| Motor direction | `MOTOR_LEFT_INVERT`, `MOTOR_RIGHT_INVERT` | false |
| Motor-pack divider values, 0 %/100 % volts | `BATTERY_R_UPPER_OHM`, `BATTERY_R_LOWER_OHM`, `BATTERY_EMPTY_V`, `BATTERY_FULL_V` | 100k, 22k, 9.6 V, 12.6 V |
| Logic-pack divider values, limits | `LOGIC_BAT_*` in brain `config.h` | 100k/100k, 3.3-4.2 V, low 15 %, critical 3.2 V |
| Ear positions | `EAR_NEUTRAL`, `EAR_PERK`, `EAR_DROOP`, `EAR_WIGGLE_DEG`, `EAR_INVERT` | 90, 55, 135, 22, false |
| Screen rotation/offset/colour | `DISPLAY_SWAP_XY`, `DISPLAY_MIRROR_X/Y`, `DISPLAY_OFFSET_X/Y`, `DISPLAY_INVERT_COLOR` | see file |
| Phone page PIN | add `#define WEB_CONTROL_PIN "1234"` in brain `config.h` | none |
| Link timeout | `LINK_TIMEOUT_MS` (body) | 5000 ms |
| Wake word | `--wake-word` in `.github/workflows/mdee.yml` | `wn9_hijason_tts2` |
| Favourite audio links | `main/boards/mdee-robot/favorites.h` | one placeholder |
| Boot logo | `main/boards/mdee-robot/boot_logo.h` (GIF bytes) | your Logo.mp4 |

---

## 14. Troubleshooting
### Build and GitHub
| Problem | Fix |
|---|---|
| `.github` missing on GitHub | Use `bash termux_push.sh`; or create `.github/workflows/mdee.yml` in the GitHub app and paste `workflow_backup/mdee.yml` |
| Push says "refusing ... workflow" | Token lacks the **workflow** scope: make a new token (repo + workflow) |
| "Authentication failed" | You typed your password; paste the token |
| No Actions runs | Repo → Settings → Actions → allow actions; branch must be `main`; use **Run workflow** |
| Red build | Open the failed step and send me the first error lines |
| Artifact is empty | Both jobs must be green; download from the finished run |

### Power and start-up
| Problem | Fix |
|---|---|
| Nothing lights up | Check the logic switch, fuse, boost output (5 V), polarity |
| Robot reboots when motors/servos move | Servos on their own 5 V buck; add 1000 µF; common ground; thicker wires; check boost ≥ 3 A |
| OLED blank | Wires on 17/18, address 0x3C, 3.3 V; OLED shares the I2C lines only with nothing else on the brain |
| Face screen white/black/garbled | Check SPI wires, backlight to 3.3 V, power |
| Face rotated, mirrored, shifted or colours inverted | Edit `DISPLAY_*` in `config.h` |
| Boot logo shows then screen stays black | Tell me; likely a screen setting or the GIF feature |

### Wi-Fi, phone and AI
| Problem | Fix |
|---|---|
| No hotspot `Xiaozhi-XXXX` | Press BOOT during start-up; reflash if needed |
| Can't open the phone page | Same Wi-Fi? Use the IP on the OLED (not `.local`); some phones block "http" – type `http://` fully; try again after 20 s |
| Phone page says offline | Wi-Fi lost; check the robot's signal bars |
| Robot hears nothing | Mic wires (4/5/6), 3.3 V, L/R pin to GND, hole not blocked; click BOOT to talk instead of the wake word |
| Wake word never works | Use the BOOT button; try `wn9_hiesp` in the workflow; keep speaker volume low |
| Robot speaks but no sound | Amp wiring 4/5/7, 5 V on the amp, speaker connected, volume ("set volume to 70") |
| Distorted or crackling sound | Weak 5 V rail/boost; add capacitor; lower volume |
| AI doesn't do actions | Ask clearly ("move forward slowly for two seconds"); tools are registered at start-up, check the OLED shows no `NO BODY LINK` |
| "Not activated" / no answers | Finish activation on xiaozhi.me with the 6-digit code; check internet |

### Body link and motion
| Problem | Fix |
|---|---|
| OLED shows `NO BODY LINK` | Cross the wires: brain 15 → body RX(44), brain 16 ← body TX(43); join GND; body firmware flashed; body powered |
| Motors don't move | 12 V pack on, TB6612 VM wired, STBY wired to GPIO4, motor-battery not below cutoff, front/rear obstacle closer than 18 cm in that direction, logic battery critical |
| Wrong direction | Invert flags (section 13) |
| Robot only turns one way / one side dead | One motor channel wiring (A01/A02, B01/B02), or driver overheating |
| Stops for no reason | An ultrasonic reads something close (floor too near, a wire in front of the sensor); view the distances on the OLED |
| Distances always `--` | Check the 5 V, TRIG (11), ECHO dividers (12 and 3), sensors facing out |
| Distances jump around | Add a 100 nF across 5 V near the sensors; soft or angled surfaces reflect poorly |
| Servo jitters or hums | Own 5 V rail + capacitor; do not drive past its stops (adjust `EAR_*` angles / limit sliders) |
| Ears move the wrong way | `EAR_INVERT true` or swap `EAR_PERK` and `EAR_DROOP` |
| Head looks the wrong way | Turn the horn 180° or edit the servo angle in your design |
| NeoPixels flicker/wrong colours | 330 Ω in the data line, short wire, 5 V supply, add a level shifter (e.g. 74AHCT125) if needed |
| White LEDs dark/never on | PCF8574 address 0x20, LED polarity (anode to 3.3 V through resistor, cathode to the pin), pull A0-A2 low |
| Battery % wrong | Calibrate the divider (section 5.3) |

### Internet audio
See the table in section 12.

---

## 15. Limits and ideas for later
* **Not included:** FM radio, video playback, camera, gesture recognition, Bluetooth control (the S3 has BLE only and it is used for Wi-Fi setup), YouTube/Spotify, AAC/OGG audio, sound-direction ("come to my voice").
* **Possible next steps:** the second microphone could give rough sound direction (it would let the robot turn toward you); a camera-capable board; an IMU for exact turn angles; an FM module (RDA5807M) with analog amp; AAC/OGG audio support.
* No echo cancellation, so very loud music can mask the wake word.
* Follow/wander use ultrasonic only; they cannot see thin objects or cliffs/stairs. **Do not run the robot near stairs or table edges unattended.**
* The code has never been compiled or tested on real hardware (see the note at the top).

---

## 16. File map and other documents
| File / folder | What it is |
|---|---|
| `MDEE_COMPLETE_GUIDE.md` | This guide |
| `TERMUX_GUIDE.md` | Step-by-step phone upload to GitHub and error table |
| `AUDIO_COMMANDS.md` | Internet audio guide |
| `BATTERY_DIVIDER.md` | Motor-pack divider |
| `LOGIC_BATTERY_DIVIDER.md` | Logic-pack divider |
| `DESIGN_NOTES.md` | How the servos, lights, screens, sensors fit into your design |
| `MDEE_README.md` | Short overview (older, shorter) |
| `termux_push.sh`, `workflow_backup/mdee.yml` | Push script and a visible copy of the workflow |
| `.github/workflows/mdee.yml` | GitHub build for both firmwares |
| `mdee_body/` | Super Mini firmware: `body_config.h` (pins, limits), `main/mdee_robot.cc` (motors, servos, sensors, lights, autonomy), `main/link.cc` (UART link) |
| `main/boards/mdee-robot/` | Brain board: `config.h` (pins), `mdee_robot_board.cc` (AI tools, buttons), `face_display.cc` (face), `oled_status.cc` (OLED), `web_control.cc` (phone page), `logic_battery.cc`, `audio_url_player.cc`, `favorites.h`, `boot_logo.h` |
| `main/` (rest), `partitions/`, `scripts/` | The XiaoZhi AI firmware and its build scripts (not edited) |

---

## 17. Quick reference card
**Talk:** wake word or click BOOT.  **Emergency stop:** double-click BOOT or STOP on the page.
**Speeds:** slow 35 · medium 55 · fast 75 · turbo 95 %.
**Phone page:** `http://<IP on OLED>/` (same Wi-Fi).
**Voice examples:** "go forward fast for 2 seconds" · "turn left" · "wander" · "follow me" · "wiggle your ears" · "rainbow lights" · "dance" · "how is your battery?" · "play lofi" · "stop the music" · "set volume to 60".
**Colours:** blue = listening · purple = thinking · green = speaking · cyan = idle · rainbow = dance/audio.
**OLED row 2:** M = motor battery, L = logic battery.
**Push to GitHub:** `cd ~/Bot2-main && bash termux_push.sh "message"`.
**Flash:** brain artifact → N16R8, body artifact → Super Mini, both at address 0x0.
**Dividers:** motor 100k/22k (×5.545) on body GPIO1 · logic 100k/100k (×2.0) on brain GPIO1 · echo 1k/2k.
