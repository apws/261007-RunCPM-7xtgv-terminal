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
    uint16_t maxCols, maxRows;
    uint8_t defaultZoom;
    Geometry zoomGeometry[4];
    uint16_t defaultRenderFlushHz;
    uint16_t defaultRgbRefreshHz;
};

inline const DisplayHalDefaults &displayHalDefaults() {
#if defined(RUNCPM_TARGET_JC8048W550)
    static constexpr DisplayHalDefaults value = {
        "ES3_5", "ESP32-S3 JC8048W550 ST7262 RGB 5-inch", 800, 480, 133, 48, 1,
        // At 3x/4x the W550 is an experimental small terminal with no pan.
        {{132, 48}, {80, 24}, {44, 16}, {32, 12}}, 30, 39};
#elif defined(RUNCPM_TARGET_JC8048W535)
    static constexpr DisplayHalDefaults value = {
        "ES3_35", "ESP32-S3 JC8048W535 AXS15231B 3.5-inch", 480, 320, 80, 32, 1,
        {{80, 32}, {40, 16}, {26, 10}, {20, 8}}, 30, 0};
#else
    static constexpr DisplayHalDefaults value = {
        "RP2_2", "RP2350 Waveshare Touch-LCD 2-inch", 320, 240, 80, 24, 1,
        {{80, 24}, {40, 12}, {26, 8}, {20, 6}}, 0, 0};
#endif
    return value;
}

inline int evenVisibleCols(int width, int zoom) {
    const int cols = width / (6 * zoom);
    return cols & ~1;
}

inline int visibleRows(int height, int zoom) { return height / (10 * zoom); }

} // namespace vt
