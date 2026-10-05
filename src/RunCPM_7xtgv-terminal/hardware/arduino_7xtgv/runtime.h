#pragma once

// Whole setup/loop implementations: runtime family, not individual board policy.
// runCPM() stays in the sketch; no code-generating or include-path macros.
#if XTGV_TERMINAL == 0
    #include "runtime_serial_runcpm.h"
#elif defined(ARDUINO_ARCH_ESP32)
    #if CONFIG_FREERTOS_UNICORE
        #include "runtime_esp32_integrated_single.h"
    #else
        #include "runtime_esp32_integrated.h"
    #endif
#elif defined(ARDUINO_ARCH_RP2040)
    #include "runtime_rp2350_integrated.h"
#else
    #error "No integrated 7xtgv runtime for this Arduino architecture"
#endif
