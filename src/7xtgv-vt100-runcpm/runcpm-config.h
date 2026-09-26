#pragma once

#if defined(ARDUINO)
#include <Arduino.h>
#endif

// ESP32 and the USB/PSRAM/core-affinity defines come from Arduino's platform
// flags. The host builder preserves them when appending target/stamp defines.

// JC8048W535: Arduino's setup()/loop() task owns core 1 and the terminal.
// A small FreeRTOS adapter pins RunCPM and SD_MMC ownership to core 0.
#if defined(RUNCPM_TARGET_JC8048W550)
#define RUNCPM_TERM_ROWS 48
#define RUNCPM_TERM_VISIBLE_COLS 133
#elif defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
#define RUNCPM_TARGET_JC8048W535 1
#define RUNCPM_TERM_ROWS 32
#define RUNCPM_TERM_VISIBLE_COLS 80
#elif defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
// The 172px panel shows at most 17 1x rows, but the backing terminal retains
// the conventional 24-row plane so higher zooms can pan vertically.
#define RUNCPM_TERM_ROWS 24
#define RUNCPM_TERM_VISIBLE_COLS 52
#elif defined(ARDUINO_ARCH_RP2040)
#define RUNCPM_TARGET_RP2350_TOUCH_LCD_2 1
#define RUNCPM_TERM_ROWS 24
#define RUNCPM_TERM_VISIBLE_COLS 52
#elif defined(ARDUINO)
#error "No RunCPM hardware target matches this Arduino core"
#else
// Host-side terminal/runtime tests use the RP2350 geometry with fake devices.
#define RUNCPM_TERM_ROWS 24
#define RUNCPM_TERM_VISIBLE_COLS 52
#endif
