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
// 3. Optional compiler policy (preprocessor attribute seam, not executable code)
// ============================================================================
#define XTGV_CPU1_IRAM_O2 0

// Retain the existing build override; Arduino's SD backend clamps to 25 MHz.
#ifndef RUNCPM_W550_SD_HZ
    #define RUNCPM_W550_SD_HZ 25000000UL
#endif
static constexpr uint32_t XTGV_STORAGE_HZ =
    RUNCPM_W550_SD_HZ > 25000000UL ? 25000000UL : RUNCPM_W550_SD_HZ;

// Only the sketch defines this guard. Functions below have ordinary external
// linkage; calling them from the terminal does NOT change task/core ownership.
#ifdef XTGV_HAL_IMPLEMENTATION
    #include <Arduino.h>
    #define FS_NO_GLOBALS // do not import fs::File into the guest adapter namespace
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
#include <SD.h>
#include <SPI.h>
#include <fcntl.h>
#include <unistd.h>

// Capture the library object before introducing RunCPM's compatibility names.
// Drivers above are already parsed; these aliases are confined to the sketch.
static fs::SDFS &xtgvArduinoSd = ::SD;
#define SD runCPMStorage

#ifndef O_READ
#define O_READ O_RDONLY
#endif
#ifndef O_WRITE
#define O_WRITE O_WRONLY
#endif

#define board_esp32
// RunCPM does not consume the FAT directory metadata copied by SdFat.
struct DirFat_t {};

class RunCPMStorage;

class RunCPMFile {
public:
    RunCPMFile() = default;
    RunCPMFile(fs::File file, RunCPMStorage *storage, const String &path)
        : file_(file), storage_(storage), path_(path) {}

    explicit operator bool() const { return bool(file_); }
    int available() { return file_.available(); }
    int read() { return file_.read(); }
    size_t read(uint8_t *buffer, size_t size) { return file_.read(buffer, size); }
    size_t write(uint8_t value) { return file_.write(value); }
    size_t write(const uint8_t *buffer, size_t size) { return file_.write(buffer, size); }
    void flush() { file_.flush(); }
    void close() { file_.close(); }
    bool isDirectory() { return file_.isDirectory(); }
    size_t size() const { return file_.size(); }
    bool seek(uint32_t position) { return file_.seek(position); }
    RunCPMFile openNextFile();
    void getName(char *buffer, size_t size) const;
    void dirEntry(DirFat_t *) const {}
    bool rename(const char *newPath);
    bool truncate(size_t size);

private:
    fs::File file_;
    RunCPMStorage *storage_ = nullptr;
    String path_;
};

class RunCPMStorage {
public:
    bool setPins() {
        SPI.begin(12, 13, 11, 10); // SCK, MISO, MOSI, CS
        return true;
    }

    bool begin() {
        pinMode(10, OUTPUT);
        digitalWrite(10, HIGH);
        return xtgvArduinoSd.begin(10, SPI, XTGV_STORAGE_HZ);
    }

    RunCPMFile open(const char *path, int flags = O_READ) {
        const String normalized = normalize(path);
        const char *mode = FILE_READ;
        if (flags & O_APPEND)
            mode = FILE_APPEND;
        else if ((flags & O_RDWR) == O_RDWR)
            mode = "r+";
        else if (flags & O_WRITE)
            mode = (flags & O_CREAT) ? FILE_WRITE : "r+";
        return RunCPMFile(xtgvArduinoSd.open(normalized, mode, bool(flags & O_CREAT)), this, normalized);
    }

    RunCPMFile open(const char *path, const char *mode) {
        const String normalized = normalize(path);
        return RunCPMFile(xtgvArduinoSd.open(normalized, mode), this, normalized);
    }

    bool exists(const char *path) { return xtgvArduinoSd.exists(normalize(path)); }
    bool remove(const char *path) { return xtgvArduinoSd.remove(normalize(path)); }
    bool mkdir(const char *path) { return xtgvArduinoSd.mkdir(normalize(path)); }
    bool rename(const char *from, const char *to) {
        return xtgvArduinoSd.rename(normalize(from), normalize(to));
    }

    String hostPath(const String &path) const { return String("/sd") + normalize(path.c_str()); }

private:
    static String normalize(const char *path) {
        while (path[0] == '.' && path[1] == '/')
            path += 2;
        if (path[0] == '/')
            return String(path);
        return String('/') + path;
    }
};

RunCPMFile RunCPMFile::openNextFile() {
    fs::File next = file_.openNextFile();
    return RunCPMFile(next, storage_, next ? String(next.path()) : String());
}

void RunCPMFile::getName(char *buffer, size_t size) const {
    if (!size)
        return;
    const char *name = file_.name();
    strncpy(buffer, name ? name : "", size - 1);
    buffer[size - 1] = 0;
}

bool RunCPMFile::rename(const char *newPath) {
    if (!storage_)
        return false;
    close();
    return storage_->rename(path_.c_str(), newPath);
}

bool RunCPMFile::truncate(size_t size) {
    if (!storage_)
        return false;
    return ::truncate(storage_->hostPath(path_).c_str(), off_t(size)) == 0;
}

// SD.h imports fs::File; the guest adapter must not collide with that name.
#define File RunCPMFile
RunCPMStorage SD;

// Preserve RunCPM's historical ESP32-only BDOS extension hook.  Existing ESP32
// board headers implement it as a no-op too.
static uint8_t esp32bdos(uint16_t) { return 0; }

XtgvBoardStorageStatus xtgvBoardStorageInit() {
    const bool pinsReady = SD.setPins();
    return {pinsReady, pinsReady && SD.begin()};
}

void xtgvBoardPrintStorageStatus(const XtgvBoardStorageStatus &status,
                                void (*write)(const char *)) {
    write("Init SD SPI bus ......");
    write(status.pinsReady ? "OK\r\n" : "FAILED\r\n");
    write("Mount MicroSD card ...");
    if (status.mediaReady) {
        char detail[80];
        snprintf(detail, sizeof(detail),
                 "OK (SPI, CS=10 MOSI=11 MISO=13 SCK=12, %lu Hz)\r\n",
                 (unsigned long)XTGV_STORAGE_HZ);
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
    // ST7262 RGB; PSRAM framebuffer, two 10-line internal DMA bounce buffers.
    auto *panel = new Arduino_ESP32RGBPanel(
        40, 41, 39, 42,          // DE, VSYNC, HSYNC, PCLK
        45, 48, 47, 21, 14,     // R0..R4
        5, 6, 7, 15, 16, 4,     // G0..G5
        8, 3, 46, 9, 1,         // B0..B4
        0, 8, 4, 8,             // horizontal timing
        0, 8, 4, 8,             // vertical timing
        1, 16000000, false, 0, 0, 8000);
    return new Arduino_RGB_Display(800, 480, panel, 0, false);
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
