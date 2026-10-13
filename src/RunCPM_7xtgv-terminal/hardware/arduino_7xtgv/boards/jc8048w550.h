#pragma once

// ============================================================================
// 1. Identity and terminal geometry (no Arduino dependencies)
// ============================================================================
static constexpr char XTGV_BOARD_TARGET_NAME[] = "jc8048w550";
static constexpr int XTGV_TERMINAL_ROWS = 48;
static constexpr int XTGV_TERMINAL_VISIBLE_COLS = 133;
static constexpr vt::DisplayHalDefaults XTGV_DISPLAY_DEFAULTS = {
    "ES3_5", "ESP32-S3 JC8048W550 ST7262 RGB 5-inch", 800, 480, 133, XTGV_TERMINAL_ROWS, 1,
    {{132, 48}, {80, 24}, {44, 16}, {32, 12}}, 30, 39, 0, nullptr};

// ============================================================================
// 2. Rendering and backlight policy
// ============================================================================
static constexpr bool XTGV_DISPLAY_NEEDS_FLUSH = true;
static constexpr bool XTGV_DISPLAY_FULL_FRAME_BUDGET = false;
static constexpr int XTGV_DISPLAY_OFFSET_X = 0;
static constexpr int XTGV_DISPLAY_OFFSET_Y = 0;
static constexpr int XTGV_BACKLIGHT_PIN = 2;

// Initial, independently tunable active-high ladder; not yet calibrated.
static constexpr uint8_t XTGV_BACKLIGHT_DUTIES[] = {
    0, 51, 52, 53, 55, 57, 60, 64, 69, 75, 82, 90, 99, 109, 120, 132,
    145, 158, 172, 186, 199, 210, 220, 229, 237, 241, 245, 248, 250,
    252, 253, 255};

// ============================================================================
// 3. Optional compiler policy and storage clock
// ============================================================================

static constexpr uint32_t XTGV_STORAGE_HZ = 20000000UL;

// Only the sketch defines this guard. Functions below have ordinary external
// linkage; calling them from the terminal does NOT change task/core ownership.
#if defined(XTGV_HAL_IMPLEMENTATION) && !XTGV_DESKTOP
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

static XtgvSdSpiDriver xtgvStorageSpi(SPI);

// Keep W550's existing Arduino SPI peripheral and GPIOs. Its RGB display
// does not use this SPI bus; RunCPM owns the card's dedicated SPI access.
SdFat32 SD;

// Preserve RunCPM's historical ESP32-only BDOS extension hook.
#define board_esp32

static uint8_t esp32bdos(uint16_t)
{
    return 0;
}

XtgvBoardStorageStatus xtgvBoardStorageInit()
{
    SPI.begin(12, 13, 11, 10); // SCK, MISO, MOSI, CS
    const bool mounted = SD.begin(SdSpiConfig(10, DEDICATED_SPI, XTGV_STORAGE_HZ, &xtgvStorageSpi));
    return {true, mounted};
}

// Called only by the card-owner task, after all configuration handles close.
bool xtgvBoardStorageSetMHz(uint8_t mhz)
{
    if (mhz < 1 || mhz > 60)
        return false;
    const bool idle = SD.card()->syncDevice();
    return idle && xtgvStorageSpi.changeClock(uint32_t(mhz) * 1000000UL);
}

void xtgvBoardPrintStorageStatus(const XtgvBoardStorageStatus& status, void (*write)(const char*))
{
    write("Init SD SPI bus ......");
    write(status.pinsReady ? "CONFIGURED\r\n" : "FAILED\r\n");
    write("Mount MicroSD card ...");
    if (status.mediaReady)
    {
        char detail[80];
        snprintf(detail, sizeof(detail), "OK (SPI, CS=10 MOSI=11 MISO=13 SCK=12, %lu Hz)\r\n",
                 (unsigned long)xtgvStorageSpi.requestedHz());
        write(detail);
    }
}

#endif

// No additional startup credit on this board.
#if defined(ARDUINO)
void xtgvBoardPrintStorageSummary(void (*write)(const char*))
{
    char message[80];
    snprintf(message, sizeof(message), "Storage: Arduino SdFat/SPI (%lu MHz)\r\n",
             (unsigned long)(xtgvStorageSpi.requestedHz() / 1000000UL));
    write(message);
}
#endif

void xtgvBoardPrintBootExtra(void (*)(const char*))
{
}

// ============================================================================
// 5. Display creation, initialization and history memory
// ============================================================================
#if XTGV_TERMINAL
Arduino_GFX* xtgvBoardCreateDisplay()
{
    // ST7262 RGB; PSRAM framebuffer, two 10-line internal DMA bounce buffers.
    auto* panel = new Arduino_ESP32RGBPanel(40, 41, 39, 42,     // DE, VSYNC, HSYNC, PCLK
                                            45, 48, 47, 21, 14, // R0..R4
                                            5, 6, 7, 15, 16, 4, // G0..G5
                                            8, 3, 46, 9, 1,     // B0..B4
                                            0, 8, 4, 8,         // horizontal timing
                                            0, 8, 4, 8,         // vertical timing
                                            1, 16000000, false, 0, 0, 8000);
    return new Arduino_RGB_Display(800, 480, panel, 0, false);
}

bool xtgvBoardBeginDisplay(Arduino_GFX* display)
{
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR < 3
    display->begin();
    return true;
    #else
    return display->begin();
    #endif
}

vt::LineHistory* xtgvBoardCreateHistory()
{
    // Ten history banks live in PSRAM, leaving internal RAM for rendering.
    void* memory = ps_malloc(sizeof(vt::LineHistory));
    return memory ? new (memory) vt::LineHistory : nullptr;
}

// ============================================================================
// 6. Physical backlight output (logical levels/fade remain terminal behavior)
// ============================================================================
void xtgvBoardBacklightInit()
{
    // Set inactive latch before enabling the active-high output.
    digitalWrite(XTGV_BACKLIGHT_PIN, LOW);
    pinMode(XTGV_BACKLIGHT_PIN, OUTPUT);
}

void xtgvBoardBacklightWrite(uint8_t duty)
{
    analogWrite(XTGV_BACKLIGHT_PIN, duty);
}
#endif

// ============================================================================
// 7. Whole-board reboot
// ============================================================================
void runCPMHardwareReboot()
{
    #if defined(ARDUINO)
    ESP.restart();
    #endif
}

#endif // XTGV_HAL_IMPLEMENTATION
