#pragma once

// JC3248W535EN / JC8048W535: vendor DEMO_PIC uses one-bit SD_MMC.
// Keep this target adapter shaped like the small SdFat subset RunCPM uses.
#define FS_NO_GLOBALS
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
#define XTGV_BOARD_BOOT_EXTRA ""
#define XTGV_BOARD_STORAGE_INIT_LABEL "Init SD_MMC pins ....."

static constexpr int RUNCPM_SD_CLK = 12;
static constexpr int RUNCPM_SD_CMD = 11;
static constexpr int RUNCPM_SD_D0 = 13;

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
    bool setPins() { return SD_MMC.setPins(RUNCPM_SD_CLK, RUNCPM_SD_CMD, RUNCPM_SD_D0); }
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

inline RunCPMFile RunCPMFile::openNextFile() {
    fs::File next = file_.openNextFile();
    return RunCPMFile(next, storage_, next ? String(next.path()) : String());
}

inline void RunCPMFile::getName(char *buffer, size_t size) const {
    if (!size)
        return;
    const char *name = file_.name();
    strncpy(buffer, name ? name : "", size - 1);
    buffer[size - 1] = 0;
}

inline bool RunCPMFile::rename(const char *newPath) {
    if (!storage_)
        return false;
    close();
    return storage_->rename(path_.c_str(), newPath);
}

inline bool RunCPMFile::truncate(size_t size) {
    if (!storage_)
        return false;
    return ::truncate(storage_->hostPath(path_).c_str(), off_t(size)) == 0;
}

using File = RunCPMFile;

// Preserve RunCPM's historical ESP32-only BDOS extension hook.  Existing ESP32
// board headers implement it as a no-op too.
static uint8_t esp32bdos(uint16_t) { return 0; }
