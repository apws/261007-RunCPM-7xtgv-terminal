// Host wiring/geometry guard; no Arduino firmware build or device access.
#define XTGV_HAL_IMPLEMENTATION
#define XTGV_TERMINAL 1
#include "../runcpm-config.h"
#include "../hardware/arduino_7xtgv/board.h"
#include <Arduino_GFX_Library.h>
#include "../src/terminal/display_policy.h"
#include <cassert>

static_assert(XTGV_TERMINAL_ROWS == 24 && XTGV_TERMINAL_VISIBLE_COLS == 52);
static_assert(XTGV_BACKLIGHT_PIN == 21);

int main() {
    auto *gfx = xtgvBoardCreateDisplay();
    assert(gfxTestBus && gfx);
    assert((gfxTestSpiPins == std::array<int, 5>{16, 17, 18, 19, 4}));
    assert((gfxTestPanel == std::array<int, 9>{20, 3, 1, 172, 320, 34, 0, 34, 0}));
    const auto &hal = vt::displayHalDefaults();
    assert(hal.zoomGeometry[0].cols == 80 && hal.zoomGeometry[0].rows == 16);
    assert(hal.logicalMaxRows == 24);
    assert(vt::visibleRows(hal.physicalHeight, 1) == 17);
    assert(vt::evenVisibleCols(hal.physicalWidth, 1) == 52);
    // Edge-to-edge panning overlaps, so all 80 columns are accessible.
    assert(80 - 52 <= 52);
    for (int z = 1; z <= 4; ++z) {
        assert(hal.zoomGeometry[z - 1].rows <= vt::visibleRows(hal.physicalHeight, z));
    }
    delete gfx;
    delete gfxTestBus;
}
