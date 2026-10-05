#pragma once

// ============================================================================
// 1. Identity and terminal geometry (no Arduino dependencies)
// ============================================================================
static constexpr char XTGV_BOARD_TARGET_NAME[] = "rp2350-lcd-1-47-a";
static constexpr int XTGV_TERMINAL_ROWS = 24;
static constexpr int XTGV_TERMINAL_VISIBLE_COLS = 52;
static constexpr vt::DisplayHalDefaults XTGV_DISPLAY_DEFAULTS = {
    "RP2_147", "RP2350 Waveshare LCD 1.47-inch A", 320, 172, 80, XTGV_TERMINAL_ROWS, 1,
    {{80, 16}, {40, 8}, {26, 5}, {20, 4}}, 0, 0, 10,
    "WARNING: avoid prolonged max backlight."};

// ============================================================================
// 2. Rendering and backlight policy
// ============================================================================
static constexpr bool XTGV_DISPLAY_NEEDS_FLUSH = false;
static constexpr bool XTGV_DISPLAY_FULL_FRAME_BUDGET = false;
static constexpr int XTGV_DISPLAY_OFFSET_X = 0;
static constexpr int XTGV_DISPLAY_OFFSET_Y = 0;
static constexpr int XTGV_BACKLIGHT_PIN = 21;

// Petr accepted old level 9 (duty 17) as the lit minimum, full PWM at 31.
// Trial: max(17+level-1, 17+round(238*((level-1)/30)^2.2)).
// Thermal safety at prolonged maximum has not been validated on this board.
static constexpr uint8_t XTGV_BACKLIGHT_DUTIES[] = {
    0, 17, 18, 19, 20, 21, 22, 24, 27, 30, 34, 38, 43, 49, 55, 62,
    69, 77, 85, 94, 104, 115, 126, 137, 150, 163, 176, 191, 206,
    221, 238, 255};

// ============================================================================
// 3. Optional compiler policy (preprocessor attribute seam, not executable code)
// ============================================================================

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
    SPI1.setSCK(10);
    SPI1.setTX(11);
    SPI1.setRX(12);
    const bool mounted = SD.begin(
        SdSpiConfig(15, DEDICATED_SPI, SD_SCK_MHZ(20), &xtgvStorageSpi));
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
    auto *bus = new Arduino_RPiPicoSPI(16, 17, 18, 19, 4);
    return new Arduino_ST7789(
        bus, 20, 3, true, 172, 320, // reset, rotation, IPS, native size
        34, 0, 34, 0);                  // controller window offsets
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
