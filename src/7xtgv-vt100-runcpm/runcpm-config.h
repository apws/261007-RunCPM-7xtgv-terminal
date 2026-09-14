#pragma once

#if defined(ARDUINO)
#include <Arduino.h>
#endif

// Arduino-ESP32 3.x uses ARDUINO_ARCH_ESP32 but no longer publishes ESP32.
// Arduino_GFX 1.6.4 and RunCPM's established ESP host marker still use the
// latter. Limit this compatibility name to the RGB W550 target.
#if defined(RUNCPM_TARGET_JC8048W550) && defined(ARDUINO_ARCH_ESP32) && !defined(ESP32)
#define ESP32 1
#endif

// JC8048W535: Arduino's setup()/loop() task owns core 1 and the terminal.
// A small FreeRTOS adapter pins RunCPM and SD_MMC ownership to core 0.
#if defined(RUNCPM_TARGET_JC8048W550)
#define RUNCPM_TERM_ROWS 48
#define RUNCPM_TERM_VISIBLE_COLS 133
#elif defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
#define RUNCPM_TARGET_JC8048W535 1
#define RUNCPM_TERM_ROWS 32
#define RUNCPM_TERM_VISIBLE_COLS 80
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
