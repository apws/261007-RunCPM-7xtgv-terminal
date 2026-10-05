#pragma once

// Optional legacy W535 one-bit SD_MMC adapter. The normal board uses SPI3/SdFat.

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
// 3. Optional compiler policy (preprocessor attribute seam, not executable code)
// ============================================================================

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
#include <FS.h>
#include <SD_MMC.h>
#include <fcntl.h>
#include <unistd.h>

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
    bool setPins() { return SD_MMC.setPins(12, 11, 13); }
    bool begin() { return SD_MMC.begin("/sdcard", true, false, 20000); }

    RunCPMFile open(const char *path, int flags = O_READ) {
        const String normalized = normalize(path);
        const char *mode = FILE_READ;
        if (flags & O_APPEND)
            mode = FILE_APPEND;
        else if ((flags & O_RDWR) == O_RDWR)
            mode = "r+";
        else if (flags & O_WRITE)
            mode = (flags & O_CREAT) ? FILE_WRITE : "r+";
        return RunCPMFile(SD_MMC.open(normalized, mode, bool(flags & O_CREAT)), this, normalized);
    }

    RunCPMFile open(const char *path, const char *mode) {
        const String normalized = normalize(path);
        return RunCPMFile(SD_MMC.open(normalized, mode), this, normalized);
    }

    bool exists(const char *path) { return SD_MMC.exists(normalize(path)); }
    bool remove(const char *path) { return SD_MMC.remove(normalize(path)); }
    bool mkdir(const char *path) { return SD_MMC.mkdir(normalize(path)); }
    bool rename(const char *from, const char *to) {
        return SD_MMC.rename(normalize(from), normalize(to));
    }

    void printRoot(Print &out) {
        fs::File root = SD_MMC.open("/");
        out.println("SD root:");
        for (int count = 0; root && count < 32; ++count) {
            fs::File entry = root.openNextFile();
            if (!entry)
                break;
            out.print("  ");
            out.print(entry.name());
            if (entry.isDirectory())
                out.print('/');
            out.println();
            entry.close();
        }
        root.close();
    }

    String hostPath(const String &path) const { return String("/sdcard") + normalize(path.c_str()); }

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

using File = RunCPMFile;
RunCPMStorage SD;

// Preserve RunCPM's historical ESP32-only BDOS extension hook.  Existing ESP32
// board headers implement it as a no-op too.
static uint8_t esp32bdos(uint16_t) { return 0; }

bool xtgvBoardStorageSetMHz(uint8_t) { return false; } // Not an SPI transport.

XtgvBoardStorageStatus xtgvBoardStorageInit() {
    const bool pinsReady = SD.setPins();
    return {pinsReady, pinsReady && SD.begin()};
}

void xtgvBoardPrintStorageStatus(const XtgvBoardStorageStatus &status,
                                void (*write)(const char *)) {
    write("Init SD_MMC pins .....");
    write(status.pinsReady ? "OK\r\n" : "FAILED\r\n");
    write("Mount MicroSD card ...");
    if (status.mediaReady) {
        write("OK (1-bit, CLK=12 CMD=11 D0=13)\r\n");
        if (!XTGV_TERMINAL)
            SD.printRoot(Serial);
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
