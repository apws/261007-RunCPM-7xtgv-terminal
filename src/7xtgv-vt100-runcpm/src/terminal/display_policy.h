#pragma once
#include <stdint.h>
#include "../../runcpm-config.h"

namespace vt {

struct Geometry { uint16_t cols, rows; };

// This is a policy description, not a second graphics abstraction.  The
// target still creates its Arduino_GFX object in 7xtgv-gfxsetup.h.
struct DisplayHalDefaults {
    const char *id;
    const char *name;
    uint16_t physicalWidth, physicalHeight;
    // Terminal-RAM limits, independent of how many cells physically fit at
    // the selected zoom. Larger configured planes use manual viewports.
    uint16_t logicalMaxCols, logicalMaxRows;
    uint8_t defaultZoom;
    Geometry zoomGeometry[4];
    uint16_t defaultRenderFlushHz;
    uint16_t defaultRgbRefreshHz;
    uint16_t diskIndicatorYOffset; // physical pixels; rounded-panel clearance
    const char *bootWarning; // optional plain text; nullptr means no notice
};

inline const DisplayHalDefaults &displayHalDefaults() {
#if defined(RUNCPM_TARGET_JC8048W550)
    static constexpr DisplayHalDefaults value = {
        "ES3_5", "ESP32-S3 JC8048W550 ST7262 RGB 5-inch", 800, 480, 133, 48, 1,
        // At 3x/4x the W550 is an experimental small terminal with no pan.
        {{132, 48}, {80, 24}, {44, 16}, {32, 12}}, 30, 39, 0, nullptr};
#elif defined(RUNCPM_TARGET_JC8048W535)
    static constexpr DisplayHalDefaults value = {
        "ES3_35", "ESP32-S3 JC8048W535 AXS15231B 3.5-inch", 480, 320, 80, 32, 1,
        {{80, 32}, {40, 16}, {26, 10}, {20, 8}}, 30, 0, 0, nullptr};
#elif defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
    static constexpr DisplayHalDefaults value = {
        "RP2_147", "RP2350 Waveshare LCD 1.47-inch A", 320, 172, 80, 24, 1,
        {{80, 16}, {40, 8}, {26, 5}, {20, 4}}, 0, 0, 10,
        // Precaution requested by Petr; C6's vendor warning is not proof of
        // the same thermal limit for this RP2350 board.
        "WARNING: avoid prolonged max backlight."};
#else
    static constexpr DisplayHalDefaults value = {
        "RP2_2", "RP2350 Waveshare Touch-LCD 2-inch", 320, 240, 80, 24, 1,
        {{80, 24}, {40, 12}, {26, 8}, {20, 6}}, 0, 0, 0, nullptr};
#endif
    return value;
}

inline int evenVisibleCols(int width, int zoom) {
    const int cols = width / (6 * zoom);
    return cols & ~1;
}

inline int visibleRows(int height, int zoom) { return height / (10 * zoom); }

} // namespace vt
