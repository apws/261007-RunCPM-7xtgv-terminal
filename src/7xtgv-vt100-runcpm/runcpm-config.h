#pragma once

#if defined(ARDUINO)
#include <Arduino.h>
#endif

// JC8048W535: Arduino's setup()/loop() task owns core 1 and the terminal.
// A small FreeRTOS adapter pins RunCPM and SD_MMC ownership to core 0.
#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
#define RUNCPM_TARGET_JC8048W535 1
#define RUNCPM_TERM_ROWS 32
#define RUNCPM_TERM_VISIBLE_COLS 80
#else
#define RUNCPM_TERM_ROWS 24
#define RUNCPM_TERM_VISIBLE_COLS 52
#endif
