#ifndef _MDEE_CONFIG_H_
#define _MDEE_CONFIG_H_

#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>

// ===================== AUDIO (shared I2S bus, duplex) =====================
// INMP441 mic + MAX98357A amp share BCLK and WS. INMP441: L/R -> GND, VDD -> 3V3.
// MAX98357A: VIN -> 5V, GAIN/SD left default (SD floating = enabled, L+R/2).
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_4
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6   // INMP441 SD
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7   // MAX98357A DIN

// ===================== FACE DISPLAY: ST7789 1.69" 240x280 (SPI) =====================
// BL pin tied to 3V3.
#define DISPLAY_MOSI_PIN GPIO_NUM_11
#define DISPLAY_CLK_PIN  GPIO_NUM_12
#define DISPLAY_CS_PIN   GPIO_NUM_10
#define DISPLAY_DC_PIN   GPIO_NUM_9
#define DISPLAY_RST_PIN  GPIO_NUM_8
#define DISPLAY_SPI_MODE 0
// Landscape (swap_xy). If the face is rotated/mirrored/shifted, tweak these 5 lines only.
#define DISPLAY_WIDTH    280
#define DISPLAY_HEIGHT   240
#define DISPLAY_SWAP_XY  true
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_INVERT_COLOR true
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X 20
#define DISPLAY_OFFSET_Y 0

// ===================== I2C bus: 0.96" OLED (0x3C) only =====================
#define I2C_SDA_PIN GPIO_NUM_17
#define I2C_SCL_PIN GPIO_NUM_18
#define OLED_I2C_ADDR   0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// ===================== LINK to the body (ESP32-S3 Super Mini) =====================
// Cross-wire: brain TX(15) -> body RX(44), brain RX(16) <- body TX(43), GND <-> GND.
#define BODY_LINK_UART   UART_NUM_1
#define BODY_LINK_TX_PIN GPIO_NUM_15
#define BODY_LINK_RX_PIN GPIO_NUM_16
#define BODY_LINK_BAUD   115200

// Speed levels (percent) - keep identical to mdee_body/body_config.h
#define SPEED_SLOW_PCT   35
#define SPEED_MEDIUM_PCT 55
#define SPEED_FAST_PCT   75
#define SPEED_TURBO_PCT  95

// ===================== LOGIC BATTERY monitor (2 x Li-ion in parallel, measured at the terminals) =====================
// Divider on GPIO1 (ADC1_CH0): battery+ --[R1 100k]--+--[R2 100k]-- GND, 100 nF from pin to GND. Ratio 0.5.
#define LOGIC_BAT_ADC_UNIT     ADC_UNIT_1
#define LOGIC_BAT_ADC_CHANNEL  ADC_CHANNEL_0
#define LOGIC_BAT_R_UPPER_OHM  100000.0f
#define LOGIC_BAT_R_LOWER_OHM  100000.0f
#define LOGIC_BAT_FULL_V       4.20f
#define LOGIC_BAT_EMPTY_V      3.30f    // 0 % (kept above the 3.0 V cell limit for safety)
#define LOGIC_BAT_LOW_PCT      15       // warning + beep
#define LOGIC_BAT_CRITICAL_V   3.20f    // below this for 5 s: robot stops, ears droop

// ===================== BUTTON =====================
#define BOOT_BUTTON_GPIO GPIO_NUM_0

// ===================== WEB CONTROL =====================
#define WEB_CONTROL_PORT 80

#endif
