#pragma once
// Included by display_policy.h after DisplayHalDefaults is declared.
namespace vt {
static constexpr DisplayHalDefaults boardDisplayHalDefaults = {
    "RP2_147", "RP2350 Waveshare LCD 1.47-inch A", 320, 172, 80, 24, 1,
    {{80, 16}, {40, 8}, {26, 5}, {20, 4}}, 0, 0, 10,
    // Precaution requested by Petr; C6's vendor warning is not proof of
    // the same thermal limit for this RP2350 board.
    "WARNING: avoid prolonged max backlight."};
} // namespace vt
