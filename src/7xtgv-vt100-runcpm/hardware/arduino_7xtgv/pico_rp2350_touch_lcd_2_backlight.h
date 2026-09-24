#pragma once
#include <stdint.h>

// RP2350 Waveshare Touch-LCD 2-inch.
// Logical OFF=0, PWM=1..30, full ON=31; normalized 8-bit duties before polarity.
// Initial shared ladder; independently tune this module after hardware testing.
static constexpr uint8_t XTGV_BACKLIGHT_DUTIES[] = {
    0, 51, 52, 53, 55, 57, 60, 64, 69, 75, 82, 90, 99, 109, 120, 132,
    145, 158, 172, 186, 199, 210, 220, 229, 237, 241, 245, 248, 250,
    252, 253, 255};
