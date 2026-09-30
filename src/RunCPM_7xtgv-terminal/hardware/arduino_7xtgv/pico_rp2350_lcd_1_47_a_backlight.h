#pragma once
#include <stdint.h>

// RP2350-LCD-1.47-A: active-high GPIO21, independent trial table.
// Petr chose old level9 (duty17) as the new minimum and retained full PWM.
// Gamma2.2 over17..255, with strictly rising low steps:
// max(17 + level - 1, 17 + round(238 * ((level - 1) / 30)^2.2)).
// OFF=0, full ON=31; only the HAL owns PWM duties. Petr visually accepted
// this curve on RP2_147; that is not thermal validation of prolonged maximum.
static constexpr uint8_t XTGV_BACKLIGHT_DUTIES[] = {
    0, 17, 18, 19, 20, 21, 22, 24, 27, 30, 34, 38, 43, 49, 55, 62,
    69, 77, 85, 94, 104, 115, 126, 137, 150, 163, 176, 191, 206,
    221, 238, 255};
