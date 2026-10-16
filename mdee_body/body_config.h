#ifndef _MDEE_BODY_CONFIG_H_
#define _MDEE_BODY_CONFIG_H_
// ESP32-S3 SUPER MINI = "body controller". Pins use only GPIO 1-13, 15-18, 43, 44 (exposed on
// virtually every Super Mini). Check YOUR board's labels; change here only.
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>

// ---- link to the brain (N16R8): UART1, 115200 8N1. Cross TX/RX, join GND. ----
#define LINK_UART_NUM  UART_NUM_1
#define LINK_TX_PIN    GPIO_NUM_43     // board label TX -> brain RX (GPIO16)
#define LINK_RX_PIN    GPIO_NUM_44     // board label RX <- brain TX (GPIO15)
#define LINK_BAUD      115200
#define LINK_TIMEOUT_MS 5000           // no message from brain this long -> stop autonomy

// ---- battery: 3S Li-ion through divider on GPIO1 (ADC1_CH0) ----
#define BATTERY_ADC_UNIT     ADC_UNIT_1
#define BATTERY_ADC_CHANNEL  ADC_CHANNEL_0
#define BATTERY_R_UPPER_OHM  100000.0f
#define BATTERY_R_LOWER_OHM  22000.0f
// Fine calibration: meter reading / robot reading (1.00 = none). See BATTERY_WIRING.md.
#define BATTERY_CAL_FACTOR   1.00f
#define BATTERY_EMPTY_V      9.6f
#define BATTERY_FULL_V       12.6f

// ---- NeoPixel x2 ----
#define NEOPIXEL_PIN   GPIO_NUM_2
#define NEOPIXEL_COUNT 2

// ---- ultrasonic (shared TRIG; ECHO through 1k/2k dividers) ----
#define US_TRIG_PIN       GPIO_NUM_11
#define US_ECHO_FRONT_PIN GPIO_NUM_12
#define US_ECHO_REAR_PIN  GPIO_NUM_3
#define OBSTACLE_STOP_CM  18
#define OBSTACLE_SLOW_CM  40

// ---- TB6612FNG (STBY needs a 10k pulldown to GND so motors are off while booting) ----
#define MOTOR_STBY GPIO_NUM_4
#define MOTOR_AIN1 GPIO_NUM_5
#define MOTOR_AIN2 GPIO_NUM_6
#define MOTOR_PWMA GPIO_NUM_7
#define MOTOR_BIN1 GPIO_NUM_8
#define MOTOR_BIN2 GPIO_NUM_9
#define MOTOR_PWMB GPIO_NUM_10
#define MOTOR_LEFT_INVERT  false
#define MOTOR_RIGHT_INVERT false
#define SPEED_SLOW_PCT   35
#define SPEED_MEDIUM_PCT 55
#define SPEED_FAST_PCT   75
#define SPEED_TURBO_PCT  95

// ---- SG90 servos (own 5V rail) ----
#define SERVO_PAN_PIN   GPIO_NUM_13
#define SERVO_TILT_PIN  GPIO_NUM_15
#define SERVO_EAR_PIN   GPIO_NUM_16   // ONE servo swings both ears (linkage/shared axle)
#define SERVO_AUX_PIN   SERVO_EAR_PIN

// Ear angles (0-180). 90 = relaxed. If the ears move the wrong way set EAR_INVERT true.
#define EAR_NEUTRAL     90
#define EAR_PERK        55    // ears forward/up (alert, listening)
#define EAR_DROOP       135   // ears back/down (sad, sleepy)
#define EAR_WIGGLE_DEG  22    // happy wiggle amplitude
#define EAR_INVERT      false

// ---- I2C for PCF8574 (8 white LEDs, active low) ----
#define I2C_SDA_PIN  GPIO_NUM_17
#define I2C_SCL_PIN  GPIO_NUM_18
#define PCF8574_ADDR 0x20

#endif
