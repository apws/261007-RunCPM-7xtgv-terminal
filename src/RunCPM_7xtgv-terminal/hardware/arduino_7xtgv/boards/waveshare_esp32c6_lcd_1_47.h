#pragma once

// ============================================================================
// 1. Identity and terminal geometry (no Arduino dependencies)
// ============================================================================
static constexpr char XTGV_BOARD_TARGET_NAME[] = "esp32c6-lcd-1-47";
static constexpr int XTGV_TERMINAL_ROWS = 24;
static constexpr int XTGV_TERMINAL_VISIBLE_COLS = 52;
static constexpr vt::DisplayHalDefaults XTGV_DISPLAY_DEFAULTS = {
    "EC6_147", "Waveshare ESP32-C6 LCD 1.47-inch", 320, 172, 80, XTGV_TERMINAL_ROWS, 1,
    {{80, 16}, {40, 8}, {26, 5}, {20, 4}}, 0, 0, 10,
    "Backlight capped at 50%: vendor thermal advice."};

// ============================================================================
// 2. Rendering and backlight policy
// ============================================================================
static constexpr bool XTGV_DISPLAY_NEEDS_FLUSH = false;
static constexpr bool XTGV_DISPLAY_FULL_FRAME_BUDGET = false;
static constexpr int XTGV_DISPLAY_OFFSET_X = 0;
static constexpr int XTGV_DISPLAY_OFFSET_Y = 0;
static constexpr int XTGV_BACKLIGHT_PIN = 22;
// Initial curve, not a measured panel calibration. Cap duty at about 50%.
static constexpr uint8_t XTGV_BACKLIGHT_DUTIES[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 17, 20, 23, 27,
    31, 36, 41, 46, 52, 58, 65, 72, 80, 88, 96, 104, 112, 118, 123, 127};

// ============================================================================
// 3. Optional compiler policy and storage clock
// ============================================================================
static constexpr uint32_t XTGV_STORAGE_HZ = 20000000UL;

#if defined(XTGV_HAL_IMPLEMENTATION) && !XTGV_DESKTOP
    #include <Arduino.h>
    #include <esp_heap_caps.h>
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
#define SDFAT_FILE_TYPE 1
#include <SdFat.h>
#include "../sdfat_spi_driver.h"

// C6 has ONE general-purpose SPI controller. Both users share this exact
// SPIClass object: its transaction mutex protects clock/mode and chip selects.
static SPIClass xtgvSharedSpi(FSPI);
static XtgvSdSpiDriver xtgvStorageSpi(xtgvSharedSpi);
SdFat32 SD;
#define board_esp32

static uint8_t esp32bdos(uint16_t)
{
    return 0;
}

XtgvBoardStorageStatus xtgvBoardStorageInit()
{
    #if !XTGV_TERMINAL
    // Integrated setup already initialized the display. Do not toggle
    // its CS here while the terminal task might be drawing.
    digitalWrite(14, HIGH);
    pinMode(14, OUTPUT);
    #endif
    digitalWrite(4, HIGH);
    pinMode(4, OUTPUT);
    xtgvSharedSpi.begin(7, 5, 6); // SCK, MISO, MOSI; CS is per device.
    const bool mounted = SD.begin(SdSpiConfig(4, SHARED_SPI, XTGV_STORAGE_HZ, &xtgvStorageSpi));
    return {true, mounted};
}

bool xtgvBoardStorageSetMHz(uint8_t mhz)
{
    if (mhz < 1 || mhz > 60)
        return false;
    return SD.card()->syncDevice() && xtgvStorageSpi.changeClock(uint32_t(mhz) * 1000000UL);
}

void xtgvBoardPrintStorageStatus(const XtgvBoardStorageStatus& status, void (*write)(const char*))
{
    write("Init shared SD/LCD SPI bus ...CONFIGURED\r\n");
    write("Mount MicroSD card ...");
    if (status.mediaReady)
        write("OK\r\n");
}
#endif

// Internal, byte-addressable heap only. Neither flash nor LP SRAM is implied.
struct XtgvC6HeapSample
{
    size_t freeBytes;
    size_t largestBlock;
};

static XtgvC6HeapSample xtgvC6HeapBeforeTerminal = {};
static bool xtgvC6HeapCaptured = false;

static XtgvC6HeapSample xtgvC6ReadHeap()
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    return {heap_caps_get_free_size(caps), heap_caps_get_largest_free_block(caps)};
}

void xtgvBoardCaptureBootHeap()
{
    xtgvC6HeapBeforeTerminal = xtgvC6ReadHeap();
    xtgvC6HeapCaptured = true;
}

#if defined(ARDUINO)
void xtgvBoardPrintStorageSummary(void (*write)(const char*))
{
    char message[80];
    snprintf(message, sizeof(message), "Storage: Arduino SdFat/SPI (%lu MHz)\r\n",
             (unsigned long)(xtgvStorageSpi.requestedHz() / 1000000UL));
    write(message);
}
#endif

void xtgvBoardPrintBootExtra(void (*write)(const char*))
{
    // This existing banner hook runs after task creation, SD/INI/history load,
    // but before the boot CPU estimate and CCP. Snapshots are not peak usage.
    const auto after = xtgvC6ReadHeap();
    char line[96];
    if (xtgvC6HeapCaptured)
    {
        snprintf(line, sizeof(line), "Heap before: free=%lu largest=%lu B\r\n",
                 (unsigned long)xtgvC6HeapBeforeTerminal.freeBytes,
                 (unsigned long)xtgvC6HeapBeforeTerminal.largestBlock);
        write(line);
    }
    snprintf(line, sizeof(line), "Heap startup: free=%lu largest=%lu B\r\n", (unsigned long)after.freeBytes,
             (unsigned long)after.largestBlock);
    write(line);
}

// ============================================================================
// 5. Display creation, initialization and history memory
// ============================================================================
#if XTGV_TERMINAL
Arduino_GFX* xtgvBoardCreateDisplay()
{
    // Deselect the card before the panel sends its initialization commands.
    digitalWrite(4, HIGH);
    pinMode(4, OUTPUT);
    // Use Arduino SPI transactions, NOT Arduino_ESP32SPI's private bus object.
    auto* bus = new Arduino_HWSPI(15, 14, 7, 6, 5, &xtgvSharedSpi, true);
    return new Arduino_ST7789(bus, 21, 3, true, 172, 320, 34, 0, 34, 0);
}

bool xtgvBoardBeginDisplay(Arduino_GFX* display)
{
    return display->begin(20000000UL);
}

vt::LineHistory* xtgvBoardCreateHistory()
{
    // C6 has no PSRAM: the shared 100-entry budget uses ordinary SRAM.
    return new (std::nothrow) vt::LineHistory;
}

// ============================================================================
// 6. Physical backlight output
// ============================================================================
void xtgvBoardBacklightInit()
{
    digitalWrite(XTGV_BACKLIGHT_PIN, LOW);
    pinMode(XTGV_BACKLIGHT_PIN, OUTPUT);
}

void xtgvBoardBacklightWrite(uint8_t duty)
{
    analogWrite(XTGV_BACKLIGHT_PIN, duty > 127 ? 127 : duty);
}
#endif

// ============================================================================
// 7. Whole-board reboot
// ============================================================================
void runCPMHardwareReboot()
{
    ESP.restart();
}
#endif // XTGV_HAL_IMPLEMENTATION
