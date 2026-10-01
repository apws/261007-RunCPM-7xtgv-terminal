#pragma once

// Each runtime owns complete setup/loop functions; runCPM() stays in the sketch.
#if XTGV_TERMINAL == 0
#include "runtime_serial_runcpm.h"
#elif defined(RUNCPM_TARGET_JC8048W535) || defined(RUNCPM_TARGET_JC8048W550)
#include "runtime_esp32_integrated.h"
#elif defined(RUNCPM_TARGET_RP2350_TOUCH_LCD_2) || defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
#include "runtime_rp2350_integrated.h"
#else
#error "No RunCPM runtime for the selected target"
#endif
