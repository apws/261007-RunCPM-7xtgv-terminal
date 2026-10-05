#pragma once

#if defined(ARDUINO)
#include <Arduino.h>
#endif

// Product composition. Target-aware display builds opt in to the integrated
// mode; a direct Arduino build defaults to serial-only RunCPM. Host terminal
// tests must also explicitly select XTGV_TERMINAL=1.
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

#ifdef RUNCPM_HEADLESS
#error "RUNCPM_HEADLESS is obsolete; select XTGV_TERMINAL=0 for serial-only RunCPM"
#endif

// ESP32 and the USB/PSRAM/core-affinity defines come from Arduino's platform
// flags. The host builder preserves them when appending target/stamp defines.

// Every Arduino firmware build identifies exactly one board. Host-only tests
// without a target retain RP2_2 geometry as their historical default.
#if defined(ARDUINO) && \
    (defined(RUNCPM_TARGET_JC8048W535) + defined(RUNCPM_TARGET_JC8048W550) + \
     defined(RUNCPM_TARGET_JC8012P4A1C) + \
     defined(RUNCPM_TARGET_ESP32C6_LCD_1_47) + \
     defined(RUNCPM_TARGET_RP2350_LCD_1_47_A) + \
     defined(RUNCPM_TARGET_RP2350_TOUCH_LCD_2) != 1)
#error "Select exactly one RunCPM board target"
#endif

// The terminal backing plane and panel policy live in the selected board
// header, independent of RunCPM configuration.
