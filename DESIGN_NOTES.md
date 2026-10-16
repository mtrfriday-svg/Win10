# M'DEE – how each part is used (for fitting it into your design)

## Servos (SG90 ×3, body board GPIO 13 / 15 / 16) – matches your design
| Servo | Job | Build advice |
|---|---|---|
| **Neck turn** (GPIO13, "pan") | Turns the head left/right. 90° = straight ahead. | In the body top, shaft pointing up; the head's tilt bracket sits on its horn. Fit the horn centred at 90°. |
| **Neck tilt** (GPIO15, "tilt") | Nods the head up/down. 90° = level. | On the turn platform, shaft sideways, head hanging on its horn. Let the screen cable loop freely. |
| **Ears** (GPIO16) | ONE servo swings BOTH ears back and forth like a pet. | In the head. Shaft across the head (left-right axis). Easiest: a cross rod / shared axle with an ear fixed on each end, driven by the servo through a short arm + link. 90° = relaxed. |

Ear behaviour (set in `mdee_body/body_config.h`: `EAR_NEUTRAL 90`, `EAR_PERK 55`, `EAR_DROOP 135`, `EAR_WIGGLE_DEG 22`, `EAR_INVERT`):
* **Idle:** a little ear flick every 5-12 s, and ears droop when the battery is almost empty.
* **Listening:** ears perk up. **Happy/laughing/loving/silly:** ears wiggle. **Sad/sleepy/angry/embarrassed:** ears droop back. **Surprised/thinking/confused:** ears perk.
* **Voice/phone:** "wiggle your ears", "perk up", "ears down" (tool `self.robot.ears`); phone page has Ears up / down / Wiggle / Auto.
* If the ears move the opposite way to what you expect, set `EAR_INVERT true`, or swap `EAR_PERK` and `EAR_DROOP`. Keep the stroke inside the real mechanical limit of your linkage (change the three angles).
* Ear servo moves quickly (about 9° per 20 ms); neck servos are slower and smoother (3° per 20 ms). The neck fidgets randomly when idle; the dance uses neck 60↔120 and tilt 70↔110 with ear wiggle.
* SG90 torque is ~1.8 kg·cm: keep the head light and balanced; no battery in the head. Add mechanical stops or limit neck travel to about 40-140° if cables could snag. Power the servos from their own 5V rail. The ear wires and the screen cable must pass through the neck: leave a slack loop.

## Distance sensors (HC-SR04 ×2, on the body)
* **Front** faces forward, **rear** faces backward, 5-10 cm above the floor, nothing blocking the two "eyes". Each sees a narrow ~15° cone and nothing closer than ~2 cm.
* Driving forward: slows below 40 cm, stops at 18 cm. Reversing uses the rear sensor the same way.
* Spinning on the spot is allowed even when something is close, so leave clearance around the body for turning.
* They stay on the body (not the head), so they do not scan when the head turns.

## Drive
* Left motor pair on TB6612 channel A, right pair on channel B. Turn = wheels in opposite directions (pivot). Acceleration is ramped (about 0.25 s to full) and motors need ≥22 % PWM to start.
* Speeds: slow 35, medium 55, fast 75, turbo 95 %.

## Lights
* **8 white LEDs** (via PCF8574): left 4 = LEDs 0-3, right 4 = LEDs 4-7. Patterns: chase, blink, left/right turn signal, battery bar. Put them as the front bar in your picture (4 each side of the centre). Standard LEDs are one colour each, so use different coloured LEDs if you want the rainbow look; a single NeoPixel can't light a whole strip, so give each of the 2 NeoPixels a light-pipe/diffuser bar like the vertical side bars in your picture.
* **2 NeoPixels**: status colour. Blue = listening, green pulse = speaking, purple pulse = connecting, cyan pulse = standby, rainbow = dance. Place them where light shows through a diffuser (e.g. the side light bars in your picture).

## Screens
* **ST7789 1.69" (face)**: head front, landscape 280×240. Plays your M'DEE logo animation at boot (≈3.7 s), then the animated eyes and mouth. Needs a cable that survives head tilt.
* **OLED 0.96"**: put it where you can read it (chest or back): Wi-Fi bars, battery V/%, mode, distances, what you said and the AI's reply, and the web-page IP.

## Audio
* **Mic (INMP441)**: hole facing outward, unblocked, at least 5 cm from the speaker and away from motors/gearboxes (noise). Foam around it helps.
* **Speaker (MAX98357A + 4-8 Ω 3 W)**: give it a small enclosed chamber with a port. Do not put it touching the mic board.

## Battery and wiring
* 12V pack → motors; buck converters → 5V logic and a separate 5V for servos. Divider resistors to GPIO1 on the body board read the pack voltage.
* The brain and body boards are joined by 3 wires (TX, RX, GND), so keep the boards close.
