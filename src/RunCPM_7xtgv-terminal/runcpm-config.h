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

// The terminal backing plane and panel policy live in the selected board
// header, independent of RunCPM configuration.
