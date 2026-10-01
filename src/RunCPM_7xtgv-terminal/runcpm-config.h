#pragma once

#if defined(ARDUINO)
#include <Arduino.h>
#endif

// Product composition. Target-aware display builds opt in to the integrated
// mode; a direct Arduino build defaults to serial-only RunCPM. Host terminal
// tests keep their existing behavior by not deriving RUNCPM_HEADLESS here.
#ifndef XTGV_RUNCPM
#define XTGV_RUNCPM 1
#endif
#ifndef XTGV_TERMINAL
#define XTGV_TERMINAL 0
#endif
#ifndef XTGV_CCP_7XF
#define XTGV_CCP_7XF 0
#endif

#if (XTGV_RUNCPM != 0 && XTGV_RUNCPM != 1) || \
    (XTGV_TERMINAL != 0 && XTGV_TERMINAL != 1) || \
    (XTGV_CCP_7XF != 0 && XTGV_CCP_7XF != 1)
#error "XTGV_RUNCPM, XTGV_TERMINAL and XTGV_CCP_7XF must be 0 or 1"
#endif
#if XTGV_RUNCPM == 0 && XTGV_TERMINAL == 1
#error "Standalone XTGV_TERMINAL is reserved, not implemented"
#elif XTGV_RUNCPM == 0
#error "Enable XTGV_RUNCPM or XTGV_TERMINAL"
#endif

#if defined(ARDUINO)
#if XTGV_TERMINAL == 0
#ifndef RUNCPM_HEADLESS
#define RUNCPM_HEADLESS 1
#endif
#elif defined(RUNCPM_HEADLESS)
#error "RUNCPM_HEADLESS conflicts with XTGV_TERMINAL=1"
#endif
#endif

// ESP32 and the USB/PSRAM/core-affinity defines come from Arduino's platform
// flags. The host builder preserves them when appending target/stamp defines.

// Every Arduino firmware build identifies exactly one board. Host-only tests
// without a target retain RP2_2 geometry as their historical default.
#if defined(ARDUINO) && \
    (defined(RUNCPM_TARGET_JC8048W535) + defined(RUNCPM_TARGET_JC8048W550) + \
     defined(RUNCPM_TARGET_RP2350_LCD_1_47_A) + \
     defined(RUNCPM_TARGET_RP2350_TOUCH_LCD_2) != 1)
#error "Select exactly one RunCPM board target"
#endif

// JC8048W535: Arduino's setup()/loop() task owns core 1 and the terminal.
// A small FreeRTOS adapter pins RunCPM and SD_MMC ownership to core 0.
#if defined(RUNCPM_TARGET_JC8048W550)
#define RUNCPM_TERM_ROWS 48
#define RUNCPM_TERM_VISIBLE_COLS 133
#elif defined(RUNCPM_TARGET_JC8048W535)
#define RUNCPM_TERM_ROWS 32
#define RUNCPM_TERM_VISIBLE_COLS 80
#elif defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
// The 172px panel shows at most 17 1x rows, but the backing terminal retains
// the conventional 24-row plane so higher zooms can pan vertically.
#define RUNCPM_TERM_ROWS 24
#define RUNCPM_TERM_VISIBLE_COLS 52
#elif defined(RUNCPM_TARGET_RP2350_TOUCH_LCD_2)
#define RUNCPM_TERM_ROWS 24
#define RUNCPM_TERM_VISIBLE_COLS 52
#elif defined(ARDUINO)
#error "No RunCPM hardware target matches this Arduino core"
#else
// Host-side terminal/runtime tests use the RP2350 geometry with fake devices.
#define RUNCPM_TERM_ROWS 24
#define RUNCPM_TERM_VISIBLE_COLS 52
#endif
