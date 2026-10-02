#pragma once

// ============================================================================
// 1. Identity and terminal geometry (no Arduino dependencies)
// ============================================================================
static constexpr char XTGV_BOARD_TARGET_NAME[] = "rp2350-touch-lcd-2";
static constexpr int XTGV_TERMINAL_ROWS = 24;
static constexpr int XTGV_TERMINAL_VISIBLE_COLS = 52;
static constexpr vt::DisplayHalDefaults XTGV_DISPLAY_DEFAULTS = {
    "RP2_2", "RP2350 Waveshare Touch-LCD 2-inch", 320, 240, 80, XTGV_TERMINAL_ROWS, 1,
    {{80, 24}, {40, 12}, {26, 8}, {20, 6}}, 0, 0, 0, nullptr};

// ============================================================================
// 2. Rendering and backlight policy
// ============================================================================
static constexpr bool XTGV_DISPLAY_NEEDS_FLUSH = false;
static constexpr bool XTGV_DISPLAY_FULL_FRAME_BUDGET = false;
static constexpr int XTGV_DISPLAY_OFFSET_X = 0;
static constexpr int XTGV_DISPLAY_OFFSET_Y = 0;
static constexpr int XTGV_BACKLIGHT_PIN = 15;

// Petr accepted duty 3 as lit minimum, independent of RP2_147.
// Trial: max(level+2, 3+round(252*((level-1)/30)^2.2)), full ON at 31.
static constexpr uint8_t XTGV_BACKLIGHT_DUTIES[] = {
    0, 3, 4, 5, 6, 7, 8, 10, 13, 17, 21, 25, 31, 37, 43, 50,
    58, 66, 75, 85, 95, 106, 118, 130, 143, 157, 172, 187, 203,
    220, 237, 255};

// ============================================================================
// 3. Optional compiler policy (preprocessor attribute seam, not executable code)
// ============================================================================
#define XTGV_CPU1_O2 0

// Only the sketch defines this guard. Functions below have ordinary external
// linkage; calling them from the terminal does NOT change task/core ownership.
#ifdef XTGV_HAL_IMPLEMENTATION
    #include <Arduino.h>
    #if XTGV_TERMINAL
        // Parse graphics headers before the legacy SD/File compatibility aliases.
        #include <Arduino_GFX_Library.h>
        #include "../../../src/terminal/line_history.h"
        #include <new>
    #endif

// ============================================================================
// 4. Storage adapter and startup reporting (RunCPM sketch only)
// ============================================================================
#if defined(ARDUINO)
// RunCPM owns dedicated SPI1; the display uses hardware SPI0.
#include <SPI.h>
#include <SdFat.h>
#include "../sdfat_spi_driver.h"

static XtgvSdSpiDriver xtgvStorageSpi(SPI1);

SdFat32 SD;
typedef File32 File;

XtgvBoardStorageStatus xtgvBoardStorageInit() {
    SPI1.setSCK(26);
    SPI1.setTX(27);
    SPI1.setRX(24);
    const bool mounted = SD.begin(
        SdSpiConfig(25, DEDICATED_SPI, SD_SCK_MHZ(20), &xtgvStorageSpi));
    return {true, mounted};
}

// Called only by the card-owner task, after all configuration handles close.
bool xtgvBoardStorageSetMHz(uint8_t mhz) {
    if (mhz < 1 || mhz > 60) return false;
    const bool idle = SD.card()->syncDevice();
    return idle && xtgvStorageSpi.changeClock(uint32_t(mhz) * 1000000UL);
}

void xtgvBoardPrintStorageStatus(const XtgvBoardStorageStatus &status,
                                void (*write)(const char *)) {
    write("Init SD SPI bus ......");
    write("CONFIGURED\r\n");
    write("Mount MicroSD card ...");
    if (status.mediaReady) {
        char detail[48];
        snprintf(detail, sizeof(detail), "%lu Hz requested\r\n",
                 (unsigned long)xtgvStorageSpi.requestedHz());
        write(detail);
    }
}

#endif

// No additional startup credit on this board.
void xtgvBoardPrintBootExtra(void (*)(const char *)) {
}

// ============================================================================
// 5. Display creation, initialization and history memory
// ============================================================================
#if XTGV_TERMINAL
Arduino_GFX *xtgvBoardCreateDisplay() {
    // ST7789 on hardware SPI0: DC, CS, SCK, MOSI, MISO.
    // RPiPicoSPI touches MISO even though this panel is write-only.
    auto *bus = new Arduino_RPiPicoSPI(16, 17, 18, 19, 1);
    return new Arduino_ST7789(
        bus, 20, 3, true, 240, 320, // reset, rotation, IPS, native size
        0, 0, 0, 0);                  // controller window offsets
}

bool xtgvBoardBeginDisplay(Arduino_GFX *display) {
    return display->begin();
}

vt::LineHistory *xtgvBoardCreateHistory() {
    return new (std::nothrow) vt::LineHistory;
}

// ============================================================================
// 6. Physical backlight output (logical levels/fade remain terminal behavior)
// ============================================================================
void xtgvBoardBacklightInit() {
    // Set inactive latch before enabling the active-high output.
    digitalWrite(XTGV_BACKLIGHT_PIN, LOW);
    pinMode(XTGV_BACKLIGHT_PIN, OUTPUT);
}

void xtgvBoardBacklightWrite(uint8_t duty) {
    analogWrite(XTGV_BACKLIGHT_PIN, duty);
}
#endif

// ============================================================================
// 7. Whole-board reboot
// ============================================================================
void runCPMHardwareReboot() {
    #if defined(ARDUINO)
        rp2040.restart();
    #endif
}

#endif // XTGV_HAL_IMPLEMENTATION
