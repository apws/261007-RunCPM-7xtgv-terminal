#pragma once

// Whole-device reset for the selected hardware target.  This is deliberately
// a HAL operation: REBOOT.COM must reset the MCU, not merely restart CP/M.
#include <Arduino.h>
#include "../../runcpm-config.h"

#if defined(RUNCPM_TARGET_JC8048W535) || defined(RUNCPM_TARGET_JC8048W550)

inline void runCPMHardwareReboot() {
    ESP.restart();
}

#elif defined(RUNCPM_TARGET_RP2350_TOUCH_LCD_2)

inline void runCPMHardwareReboot() {
    // Arduino-Pico implements this as a watchdog whole-chip warm reboot.
    rp2040.restart();
}

#elif !defined(ARDUINO)

// Host-side runtime tests never request a hardware reboot.
inline void runCPMHardwareReboot() {}

#else
#error "No RunCPM reboot implementation for this hardware target"
#endif
