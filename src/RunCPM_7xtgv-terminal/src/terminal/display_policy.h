#pragma once
#include "../../hardware/arduino_7xtgv/board.h"

namespace vt {

// Small header-only geometry helpers; no Arduino drivers or RunCPM dependency.
inline const DisplayHalDefaults &displayHalDefaults() {
    return XTGV_DISPLAY_DEFAULTS;
}

inline int evenVisibleCols(int width, int zoom) {
    const int cols = width / (6 * zoom);
    return cols & ~1;
}

inline int visibleRows(int height, int zoom) { return height / (10 * zoom); }

} // namespace vt
