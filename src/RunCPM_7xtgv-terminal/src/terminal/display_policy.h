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

} // namespace vt

#if defined(RUNCPM_TARGET_JC8048W550)
#include "../../hardware/arduino_7xtgv/boards/jc8048w550_display.h"
#elif defined(RUNCPM_TARGET_JC8048W535)
#include "../../hardware/arduino_7xtgv/boards/jc8048w535_display.h"
#elif defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
#include "../../hardware/arduino_7xtgv/boards/pico_rp2350_lcd_1_47_a_display.h"
#else
#include "../../hardware/arduino_7xtgv/boards/pico_rp2350_touch_lcd_2_display.h"
#endif

namespace vt {

inline const DisplayHalDefaults &displayHalDefaults() {
    return boardDisplayHalDefaults;
}

inline int evenVisibleCols(int width, int zoom) {
    const int cols = width / (6 * zoom);
    return cols & ~1;
}

inline int visibleRows(int height, int zoom) { return height / (10 * zoom); }

} // namespace vt
