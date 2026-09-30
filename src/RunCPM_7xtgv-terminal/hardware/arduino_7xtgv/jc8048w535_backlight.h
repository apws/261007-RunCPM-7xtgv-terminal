#pragma once
#include <stdint.h>

// JC8048W535 / ESP32-S3, AXS15231B QSPI.
// Logical OFF=0, PWM=1..30, full ON=31; normalized 8-bit duties before polarity.
// Petr confirmed duty 1 as usable in darkness. First perceptual tuning trial:
// max(level, 1 + round(254 * ((level - 1) / 30)^2.2)), for lit levels 1..31.
// The floor keeps every 8-bit step distinct; no floating-point work at runtime.
// Gamma 2.2 is a starting approximation, not a measured panel calibration.
static constexpr uint8_t XTGV_BACKLIGHT_DUTIES[] = {
    0, 1, 2, 3, 4, 5, 6, 8, 11, 15, 19, 24, 29, 35, 41, 48,
    56, 65, 74, 84, 94, 105, 117, 129, 143, 156, 171, 186, 202,
    219, 237, 255};
