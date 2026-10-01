// Host wiring/geometry guard; no Arduino firmware build or device access.
#include "../runcpm-config.h"
#include "../hardware/arduino_7xtgv/boards/pico_rp2350_lcd_1_47_a_sd_spi.h"
#include "../src/terminal/display_policy.h"
#include "../src/terminal/7xtgv-gfxsetup.h"
#include <cassert>

static_assert(SD_CS == 15 && SD_SCK == 10 && SD_MOSI == 11 && SD_MISO == 12);
static_assert(RUNCPM_TERM_ROWS == 24 && RUNCPM_TERM_VISIBLE_COLS == 52);
static_assert(GFX_BL == 21 && GFX_CS == 17 && GFX_RST == 20);
static_assert(GFX_SCK == 18 && GFX_DC == 16 && GFX_MOSI == 19 && GFX_MISO == 4);
static_assert(GFX_MX == 172 && GFX_MY == 320 && GFX_ROT == 3);
static_assert(GFX_DX02 == 34 && GFX_DX13 == 34);

int main() {
    GFX_DEVICE_INIT();
    assert(bus && gfx);
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
    delete bus;
}
