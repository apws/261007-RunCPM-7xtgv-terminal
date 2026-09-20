#pragma once

// JC8048W550 vendor SD example: independent SPI card, not W535 SDMMC.
// Keep Arduino's library object separate from RunCPM's storage adapter.
#define FS_NO_GLOBALS
#include <SD.h>
#include <SPI.h>
#include <fcntl.h>
#include <unistd.h>

static fs::SDFS &RUNCPM_ESP32_SD_LIBRARY = ::SD;
// The RunCPM sources intentionally use an unqualified SD adapter.  Rename
// that adapter at preprocessing time, leaving the SD library's separately
// compiled global object untouched.
#define SD runCPMStorage

#ifndef O_READ
#define O_READ O_RDONLY
#endif
#ifndef O_WRITE
#define O_WRITE O_WRONLY
#endif
#define board_esp32
static constexpr int RUNCPM_SD_CS = 10;
static constexpr int RUNCPM_SD_MOSI = 11;
static constexpr int RUNCPM_SD_MISO = 13;
static constexpr int RUNCPM_SD_SCK = 12;
#ifndef RUNCPM_W550_SD_HZ
#define RUNCPM_W550_SD_HZ 25000000UL
#endif
// Arduino-ESP32 3.1.0 libraries/SD clamps SPI cards to 25 MHz after probing.
// Clamp here too so startup reports the clock actually requested for I/O.
#define RUNCPM_W550_SD_EFFECTIVE_HZ \
    ((RUNCPM_W550_SD_HZ) > 25000000UL ? 25000000UL : (RUNCPM_W550_SD_HZ))
struct DirFat_t {};
class RunCPMStorage;
class RunCPMFile {
public:
    RunCPMFile() = default;
    RunCPMFile(fs::File file, RunCPMStorage *storage, const String &path) : file_(file), storage_(storage), path_(path) {}
    explicit operator bool() const { return bool(file_); }
    int available() { return file_.available(); }
    int read() { return file_.read(); }
    size_t read(uint8_t *buffer, size_t size) { return file_.read(buffer, size); }
    size_t write(uint8_t value) { return file_.write(value); }
    size_t write(const uint8_t *buffer, size_t size) { return file_.write(buffer, size); }
    void flush() { file_.flush(); } void close() { file_.close(); }
    bool isDirectory() { return file_.isDirectory(); } size_t size() const { return file_.size(); }
    bool seek(uint32_t position) { return file_.seek(position); }
    RunCPMFile openNextFile(); void getName(char *buffer, size_t size) const;
    void dirEntry(DirFat_t *) const {} bool rename(const char *newPath); bool truncate(size_t size);
private: fs::File file_; RunCPMStorage *storage_ = nullptr; String path_;
};
class RunCPMStorage {
public:
    bool setPins() { SPI.begin(RUNCPM_SD_SCK, RUNCPM_SD_MISO, RUNCPM_SD_MOSI, RUNCPM_SD_CS); return true; }
    bool begin() { pinMode(RUNCPM_SD_CS, OUTPUT); digitalWrite(RUNCPM_SD_CS, HIGH); return RUNCPM_ESP32_SD_LIBRARY.begin(RUNCPM_SD_CS, SPI, RUNCPM_W550_SD_EFFECTIVE_HZ); }
    RunCPMFile open(const char *path, int flags = O_READ) {
        const String normalized = normalize(path); const char *mode = FILE_READ;
        if (flags & O_APPEND) mode = FILE_APPEND;
        else if ((flags & O_RDWR) == O_RDWR) mode = "r+";
        else if (flags & O_WRITE) mode = (flags & O_CREAT) ? FILE_WRITE : "r+";
        return RunCPMFile(RUNCPM_ESP32_SD_LIBRARY.open(normalized, mode, bool(flags & O_CREAT)), this, normalized);
    }
    RunCPMFile open(const char *path, const char *mode) { const String p = normalize(path); return RunCPMFile(RUNCPM_ESP32_SD_LIBRARY.open(p, mode), this, p); }
    bool exists(const char *path) { return RUNCPM_ESP32_SD_LIBRARY.exists(normalize(path)); }
    bool remove(const char *path) { return RUNCPM_ESP32_SD_LIBRARY.remove(normalize(path)); }
    bool mkdir(const char *path) { return RUNCPM_ESP32_SD_LIBRARY.mkdir(normalize(path)); }
    bool rename(const char *from, const char *to) { return RUNCPM_ESP32_SD_LIBRARY.rename(normalize(from), normalize(to)); }
    String hostPath(const String &path) const { return String("/sd") + normalize(path.c_str()); }
private:
    static String normalize(const char *path) { while (path[0] == '.' && path[1] == '/') path += 2; return path[0] == '/' ? String(path) : String('/') + path; }
};
inline RunCPMFile RunCPMFile::openNextFile() { fs::File next = file_.openNextFile(); return RunCPMFile(next, storage_, next ? String(next.path()) : String()); }
inline void RunCPMFile::getName(char *buffer, size_t size) const { if (!size) return; const char *name = file_.name(); strncpy(buffer, name ? name : "", size - 1); buffer[size - 1] = 0; }
inline bool RunCPMFile::rename(const char *newPath) { if (!storage_) return false; close(); return storage_->rename(path_.c_str(), newPath); }
inline bool RunCPMFile::truncate(size_t size) { return storage_ && ::truncate(storage_->hostPath(path_).c_str(), off_t(size)) == 0; }
// ESP32's SD.h imports fs::File into the global namespace.  RunCPM predates
// that API and consistently uses an unqualified File, so make the adapter
// explicit for the rest of this translation unit without colliding with it.
#define File RunCPMFile
static uint8_t esp32bdos(uint16_t) { return 0; }
