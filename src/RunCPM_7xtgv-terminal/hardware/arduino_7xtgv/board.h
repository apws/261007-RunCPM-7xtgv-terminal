#pragma once

#include <stdint.h>

// Common value types and operation declarations only. Each selected header
// below contains the complete board implementation in the same section order.
// Define XTGV_HAL_IMPLEMENTATION in ONE translation unit, before its first
// inclusion of this header (the sketch). Other users see constants/declarations.
namespace vt {

struct Geometry { uint16_t cols, rows; };

// This is a policy description, not a second graphics abstraction.  The
// selected board creates the existing Arduino_GFX object.
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


class Arduino_GFX;
namespace vt { class LineHistory; }

struct XtgvBoardStorageStatus {
    bool pinsReady;
    bool mediaReady;
};

XtgvBoardStorageStatus xtgvBoardStorageInit();
bool xtgvBoardStorageSetMHz(uint8_t mhz);
void xtgvBoardPrintStorageStatus(const XtgvBoardStorageStatus &status,
                                void (*write)(const char *));
void xtgvBoardPrintBootExtra(void (*write)(const char *));
Arduino_GFX *xtgvBoardCreateDisplay();
bool xtgvBoardBeginDisplay(Arduino_GFX *display);
vt::LineHistory *xtgvBoardCreateHistory();
void xtgvBoardBacklightInit();
void xtgvBoardBacklightWrite(uint8_t duty);
void runCPMHardwareReboot();

// One direct selection. No include-path macros or per-feature dispatchers.
#if defined(RUNCPM_TARGET_JC8048W535)
    #if defined(XTGV_W535_SD_MMC)
        #include "boards/jc8048w535_sdmmc.h"
    #else
        #include "boards/jc8048w535.h"
    #endif
#elif defined(RUNCPM_TARGET_JC8012P4A1C)
    #include "boards/jc8012p4a1c.h"
#elif defined(RUNCPM_TARGET_JC8048W550)
    #include "boards/jc8048w550.h"
#elif defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
    #include "boards/pico_rp2350_lcd_1_47_a.h"
#elif defined(RUNCPM_TARGET_RP2350_TOUCH_LCD_2) || !defined(ARDUINO)
    // Native tests without a target retain the historical RP2_2 geometry.
    #include "boards/pico_rp2350_touch_lcd_2.h"
#else
    #error "No 7xtgv board header matches this Arduino build"
#endif

// Header-only identity queries; no hardware access or stored runtime state.
// Compiled configuration, not a claim to detect the physical board's wiring.
inline const char *xtgvFirmwareTarget() {
    return XTGV_BOARD_TARGET_NAME;
}
inline const char *xtgvFirmwareBuild() {
#ifdef XTGV_BUILD_STAMP
#define XTGV_ID_STRING_INNER(value) #value
#define XTGV_ID_STRING(value) XTGV_ID_STRING_INNER(value)
    return XTGV_ID_STRING(XTGV_BUILD_STAMP);
#else
    return "unstamped";
#endif
}
