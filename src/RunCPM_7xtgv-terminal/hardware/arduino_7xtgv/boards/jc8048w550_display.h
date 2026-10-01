#pragma once
// Included by display_policy.h after DisplayHalDefaults is declared.
namespace vt {
static constexpr DisplayHalDefaults boardDisplayHalDefaults = {
    "ES3_5", "ESP32-S3 JC8048W550 ST7262 RGB 5-inch", 800, 480, 133, 48, 1,
    // At 3x/4x the W550 is an experimental small terminal with no pan.
    {{132, 48}, {80, 24}, {44, 16}, {32, 12}}, 30, 39, 0, nullptr};
} // namespace vt
