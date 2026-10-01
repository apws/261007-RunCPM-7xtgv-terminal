#pragma once

// Product-owned implementations of optional, inert-by-default RunCPM seams.
// Include before the RunCPM core headers, after board.h has selected policy.
#if XTGV_TERMINAL || XTGV_CCP_7XF
#include "../../forced_warm_return.h"
#endif

#if XTGV_TERMINAL
static bool _xtgvAbortRequested() {
    return forcedWarmReturn().cpuTakeRequest();
}
#define XTGV_ABORT_REQUESTED _xtgvAbortRequested
#endif

// This is a target choice, not a property of every ESP32 or CPU model.
// Other ESP32 boards may opt in only after a measured target build.
#if defined(XTGV_CPU1_IRAM_O2) && XTGV_CPU1_IRAM_O2
#if !defined(ARDUINO_ARCH_ESP32)
#error "XTGV_CPU1_IRAM_O2 requires an ESP32 Arduino target"
#endif
#define XTGV_Z80RUN_ATTRIBUTES IRAM_ATTR __attribute__((optimize("O2")))
#endif
