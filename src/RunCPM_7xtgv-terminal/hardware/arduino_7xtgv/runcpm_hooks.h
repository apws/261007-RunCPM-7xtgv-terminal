#pragma once

// Product-owned implementations of optional, inert-by-default RunCPM seams.
// Include before the RunCPM core headers, after board.h has selected policy.
#if XTGV_TERMINAL || XTGV_CCP_7XF
#include "../../forced_warm_return.h"
#endif

#if XTGV_TERMINAL
static bool _xtgvAbortRequested() {
    #if defined(ARDUINO_ARCH_ESP32) && CONFIG_FREERTOS_UNICORE
        // Existing periodic guest poll is outside SD transactions. No core
        // edits: briefly block compute-only COMs so the idle task can run.
        static TickType_t lastYield = 0;
        const TickType_t now = xTaskGetTickCount();
        if (TickType_t(now - lastYield) >= pdMS_TO_TICKS(25)) {
            lastYield = now;
            vTaskDelay(1);
        }
    #endif
    return forcedWarmReturn().cpuTakeRequest();
}
#define XTGV_ABORT_REQUESTED _xtgvAbortRequested
#endif

// ESP32 CPU1 dispatcher placement, independent of compiler optimization.
// Each target still needs an IRAM-capacity/link check and runtime benchmarks.
// Optimization follows the whole-build flags on every target.
#if defined(ARDUINO_ARCH_ESP32)
#define XTGV_Z80RUN_ATTRIBUTES IRAM_ATTR
#endif
