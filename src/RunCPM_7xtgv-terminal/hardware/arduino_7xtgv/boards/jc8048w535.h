#pragma once

// ============================================================================
// 1. Identity and terminal geometry (no Arduino dependencies)
// ============================================================================
static constexpr char XTGV_BOARD_TARGET_NAME[] = "jc8048w535";
static constexpr int XTGV_TERMINAL_ROWS = 32;
static constexpr int XTGV_TERMINAL_VISIBLE_COLS = 80;
static constexpr vt::DisplayHalDefaults XTGV_DISPLAY_DEFAULTS = {
    "ES3_35", "ESP32-S3 JC8048W535 AXS15231B 3.5-inch", 480, 320, 80, XTGV_TERMINAL_ROWS, 1,
    {{80, 32}, {40, 16}, {26, 10}, {20, 8}}, 30, 0, 0, nullptr};

// ============================================================================
// 2. Rendering and backlight policy
// ============================================================================
static constexpr bool XTGV_DISPLAY_NEEDS_FLUSH = true;
static constexpr bool XTGV_DISPLAY_FULL_FRAME_BUDGET = true;
static constexpr int XTGV_DISPLAY_OFFSET_X = 0;
static constexpr int XTGV_DISPLAY_OFFSET_Y = 0;
static constexpr int XTGV_BACKLIGHT_PIN = 1;

// Active-high backlight. PWM=1..30, full ON=31; Petr accepted duty 1 in
// darkness. Trial: max(level, 1 + round(254*((level-1)/30)^2.2)).
// The floor keeps low 8-bit steps distinct; gamma is not panel calibration.
static constexpr uint8_t XTGV_BACKLIGHT_DUTIES[] = {
    0, 1, 2, 3, 4, 5, 6, 8, 11, 15, 19, 24, 29, 35, 41, 48,
    56, 65, 74, 84, 94, 105, 117, 129, 143, 156, 171, 186, 202,
    219, 237, 255};

// ============================================================================
// 3. Optional compiler policy and storage clock
// ============================================================================
#define XTGV_CPU1_O2 1
static constexpr uint32_t XTGV_STORAGE_HZ = 20000000UL;

// Only the sketch defines this guard. Functions below have ordinary external
// linkage; calling them from the terminal does NOT change task/core ownership.
#ifdef XTGV_HAL_IMPLEMENTATION
    #include <Arduino.h>
    #if XTGV_TERMINAL
        #include <Arduino_GFX_Library.h>
        #include "../../../src/terminal/line_history.h"
        #include <new>
    #endif

// ============================================================================
// 4. Storage adapter and startup reporting (RunCPM sketch only)
// ============================================================================
#if defined(ARDUINO)
#include <SPI.h>
// The current CP/M card is FAT32. Match SdFat32::open() with File32 in the
// RunCPM abstraction; SdFat otherwise selects its FAT/exFAT FsFile alias.
#define SDFAT_FILE_TYPE 1
#include <SdFat.h>
#include "../sdfat_spi_driver.h"

static XtgvSdSpiDriver xtgvStorageSpi(xtgvSdSpi);

// The display QSPI driver owns SPI2. On ESP32-S3 Arduino HSPI is SPI3; its
// signals can use the card's existing GPIOs through the GPIO matrix.
static SPIClass xtgvSdSpi(HSPI);
SdFat32 SD;

// Preserve RunCPM's historical ESP32-only BDOS extension hook.
#define board_esp32
static uint8_t esp32bdos(uint16_t) { return 0; }

XtgvBoardStorageStatus xtgvBoardStorageInit() {
    xtgvSdSpi.begin(12, 13, 11, 10); // SCK, MISO, MOSI, CS
    const bool mounted = SD.begin(
        SdSpiConfig(10, DEDICATED_SPI, XTGV_STORAGE_HZ, &xtgvStorageSpi));
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
    write(status.pinsReady ? "CONFIGURED\r\n" : "FAILED\r\n");
    write("Mount MicroSD card ...");
    if (status.mediaReady) {
        char detail[80];
        snprintf(detail, sizeof(detail),
                 "OK (SPI, CS=10 MOSI=11 MISO=13 SCK=12, %lu Hz)\r\n",
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
    // AXS15231B controller rotation is broken; rotate the canvas instead.
    auto *bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
    auto *panel = new Arduino_AXS15231B(
        bus, GFX_NOT_DEFINED, 0, false, 320, 480);
    return new Arduino_Canvas(320, 480, panel, 0, 0, 1);
}

bool xtgvBoardBeginDisplay(Arduino_GFX *display) {
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR < 3
        display->begin();
        return true;
    #else
        return display->begin();
    #endif
}

vt::LineHistory *xtgvBoardCreateHistory() {
    // Ten history banks live in PSRAM, leaving internal RAM for rendering.
    void *memory = ps_malloc(sizeof(vt::LineHistory));
    return memory ? new (memory) vt::LineHistory : nullptr;
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
        ESP.restart();
    #endif
}

#endif // XTGV_HAL_IMPLEMENTATION
