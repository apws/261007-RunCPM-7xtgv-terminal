#pragma once
#include <stdint.h>

// RP2350 Waveshare Touch-LCD 2-inch.
// Logical OFF=0, PWM=1..30, full ON=31; normalized 8-bit duties before polarity.
// Petr found duty 3 the lowest useful full-color level on RP2_2.
// Keep gamma 2.2, rescaled to 3..255; distinct low steps use a rising floor:
// max(level + 2, 3 + round(252 * ((level - 1) / 30)^2.2)), levels 1..31.
// Independent table; Petr visually accepted this curve on RP2_2.
static constexpr uint8_t XTGV_BACKLIGHT_DUTIES[] = {
    0, 3, 4, 5, 6, 7, 8, 10, 13, 17, 21, 25, 31, 37, 43, 50,
    58, 66, 75, 85, 95, 106, 118, 130, 143, 157, 172, 187, 203,
    220, 237, 255};
