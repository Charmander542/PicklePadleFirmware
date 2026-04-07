#pragma once

#include "driver/gpio.h"

// Single I2C bus: BNO055 (0x28 or 0x29 from SA0), TCA9548A (0x70), OLED on the main segment.
// Only DRV2605 haptics sit behind the mux downstream ports.
#define BUS_SDA GPIO_NUM_26 // 21 for the ESP32 Dev Module 26 for PCB
#define BUS_SCL GPIO_NUM_27 // 22 for the ESP32 Dev Module 27 for PCB

#define TCA9548A_ADDR 0x70
#define DRV_I2C_ADDR_A 0x5A
#define DRV_I2C_ADDR_B 0x5B

#define OLED_I2C_ADDR 0x3C
// SSD1306 SA0=1 modules use 0x3D; DisplayManager probes both.
#define OLED_I2C_ADDR_ALT 0x3D
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32


// SD card (SPI)
#define SD_CS GPIO_NUM_15
#define SD_MOSI GPIO_NUM_12 // 27 for the ESP32 Dev Module 12 for PCB
#define SD_MISO GPIO_NUM_13
#define SD_CLK GPIO_NUM_14

// NeoPixel strip (data pin). Count / brightness: tweak in include/paddle_fx.h
#define NEOPIXEL_PIN GPIO_NUM_2

#define SPEAKER_PWM GPIO_NUM_16
#define SPEAKER_LEDC_CHAN 0
#define SPEAKER_LEDC_BITS 10

// BOOT / IO0 — must float high at reset for normal boot; button pulls low.
#define BUTTON_PIN GPIO_NUM_0

// Legacy camera block (avoid these GPIOs if camera populated).
#define XCLK_GPIO_NUM GPIO_NUM_0
#define SIOD_GPIO_NUM GPIO_NUM_26
#define SIOC_GPIO_NUM GPIO_NUM_27
#define Y9_GPIO_NUM GPIO_NUM_35
#define Y8_GPIO_NUM GPIO_NUM_34
#define Y7_GPIO_NUM GPIO_NUM_39
#define Y6_GPIO_NUM GPIO_NUM_36
#define Y5_GPIO_NUM GPIO_NUM_21
#define Y4_GPIO_NUM GPIO_NUM_19
#define Y3_GPIO_NUM GPIO_NUM_18
#define Y2_GPIO_NUM GPIO_NUM_5
#define VSYNC_GPIO_NUM GPIO_NUM_25
#define HREF_GPIO_NUM GPIO_NUM_23
#define PCLK_GPIO_NUM GPIO_NUM_22
