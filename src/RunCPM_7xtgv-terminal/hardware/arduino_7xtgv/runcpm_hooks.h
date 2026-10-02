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

// ESP32 CPU1 dispatcher placement, independent of compiler optimization.
// Each target still needs an IRAM-capacity/link check and runtime benchmarks.
// W535 retains its measured function-local O2; other targets keep build flags.
#if defined(ARDUINO_ARCH_ESP32)
#if defined(XTGV_CPU1_O2) && XTGV_CPU1_O2
#define XTGV_Z80RUN_ATTRIBUTES IRAM_ATTR __attribute__((optimize("O2")))
#else
#define XTGV_Z80RUN_ATTRIBUTES IRAM_ATTR
#endif
#endif
