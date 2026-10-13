#ifndef ABSTRACT_H
#define ABSTRACT_H

#ifdef PROFILE
    #define printf(a, b) ((void)0) // Profiling must not write USB from core 1
#endif

#if defined ARDUINO_SAM_DUE || defined ADAFRUIT_GRAND_CENTRAL_M4
    #define HostOS 0x01
#endif
#if defined CORE_TEENSY
    #define HostOS 0x04
#endif
#if defined ESP32 || defined ARDUINO_ARCH_ESP32
    #define HostOS 0x05
#endif
#if defined _STM32_DEF_
    #define HostOS 0x06
#endif
#if defined ARDUINO_ARCH_RP2040
    #define HostOS 0x07
#endif

static void _xtgvDiskActivity(bool active);

/* Memory abstraction functions */
/*===============================================================================*/
uint16 _RamLoad(uint8 *filename, uint16 address, uint16 maxsize) {
    File f;
    uint16 bytesread = 0;

    _xtgvDiskActivity(true);
    if (f = SD.open((char *)filename, FILE_READ)) {
        while (f.available()) {
            _RamWrite(address++, f.read());
            bytesread++;
            if (maxsize && bytesread >= maxsize)
                break;
        }
        f.close();
    }
    _xtgvDiskActivity(false);
    return (bytesread);
}

// Boards without a dedicated activity LED leave LED undefined.
static void _xtgvDiskActivity(bool active) {
#if XTGV_TERMINAL
    terminalDiskActivity(active);
#endif
#ifdef LED
    digitalWrite(LED, (active ? HIGH : LOW) ^ LEDinv);
#endif
}

/* Filesystem (disk) abstraction functions */
/*===============================================================================*/
File rootdir, userdir;
#define FOLDERCHAR '/'
#define FILEBASE "./"

typedef struct {
    uint8 dr;
    uint8 fn[8];
    uint8 tp[3];
    uint8 ex, s1, s2, rc;
    uint8 al[16];
    uint8 cr, r0, r1, r2;
} CPM_FCB;

typedef struct {
    uint8 dr;
    uint8 fn[8];
    uint8 tp[3];
    uint8 ex, s1, s2, rc;
    uint8 al[16];
} CPM_DIRENTRY;

static DirFat_t fileDirEntry;

// Host files are normally opened/closed per 128-byte record, not per guest
// FCB. The terminal INI cache therefore needs the logical BDOS close too.
#if XTGV_TERMINAL
static void _xtgvModifiedFileClosed(const char *path) {
    terminalConfigFileClosed(path);
}
static void _xtgvConfigGuestIdle() {
    terminalConfigGuestIdle();
}
#define XTGV_MODIFIED_FILE_CLOSED _xtgvModifiedFileClosed
#define XTGV_CONFIG_GUEST_IDLE _xtgvConfigGuestIdle
static bool _xtgvConfigAccess(const char *path, bool writing) {
    return terminalConfigFileAccess(path, writing);
}
#else
static bool _xtgvConfigAccess(const char *, bool) { return true; }
#endif

bool _sys_exists(uint8 *filename) {
    _xtgvDiskActivity(true);
    const bool exists = SD.exists((const char *)filename);
    _xtgvDiskActivity(false);
    return exists;
}

File _sys_fopen_w(uint8 *filename) {
    if (!_xtgvConfigAccess((const char *)filename, true)) return File();
    _xtgvDiskActivity(true);
    File file = SD.open((char *)filename, O_CREAT | O_WRITE);
    _xtgvDiskActivity(false);
    return file;
}

int _sys_fputc(uint8 ch, File &f) {
    return (f.write(ch));
}

void _sys_fflush(File &f) {
    f.flush();
}

void _sys_fclose(File &f) {
    f.close();
}

int _sys_select(uint8 *disk) {
    uint8 result = FALSE;
    File f;

    _xtgvDiskActivity(true);
    if (f = SD.open((char *)disk, O_READ)) {
        if (f.isDirectory())
            result = TRUE;
        f.close();
    }
    _xtgvDiskActivity(false);
    return (result);
}

long _sys_filesize(uint8 *filename) {
    long l = -1;
    File f;

    _xtgvDiskActivity(true);
    if (f = SD.open((char *)filename, O_RDONLY)) {
        l = f.size();
        f.close();
    }
    _xtgvDiskActivity(false);
    return (l);
}

int _sys_openfile(uint8 *filename) {
    if (!_xtgvConfigAccess((const char *)filename, false)) return 0;
    File f;
    int result = 0;

    _xtgvDiskActivity(true);
    f = SD.open((char *)filename, O_READ);
    if (f) {
        f.dirEntry(&fileDirEntry);
        f.close();
        result = 1;
    }
    _xtgvDiskActivity(false);
    return (result);
}

int _sys_makefile(uint8 *filename) {
    if (!_xtgvConfigAccess((const char *)filename, true)) return 0;
    File f;
    int result = 0;

    _xtgvDiskActivity(true);
    f = SD.open((char *)filename, O_CREAT | O_WRITE);
    if (f) {
        f.close();
        result = 1;
    }
    _xtgvDiskActivity(false);
    return (result);
}

int _sys_deletefile(uint8 *filename) {
    if (!_xtgvConfigAccess((const char *)filename, true)) return 0;
    _xtgvDiskActivity(true);
    const int result = SD.remove((char *)filename);
    _xtgvDiskActivity(false);
    return result;
}

int _sys_renamefile(uint8 *filename, uint8 *newname) {
    if (!_xtgvConfigAccess((const char *)filename, true) ||
        !_xtgvConfigAccess((const char *)newname, true)) return 0;
    File f;
    int result = 0;

    _xtgvDiskActivity(true);
    f = SD.open((char *)filename, O_WRITE | O_APPEND);
    if (f) {
        if (f.rename((char *)newname)) {
            f.close();
            result = 1;
        }
    }
    _xtgvDiskActivity(false);
    return (result);
}

#ifdef DEBUGLOG
void _sys_logbuffer(uint8 *buffer) {
    #ifdef CONSOLELOG
    puts((char *)buffer);
    #else
    File f;
    uint8 s = 0;
    while (*(buffer + s)) // Computes buffer size
        ++s;
    if (f = SD.open(LogName, O_CREAT | O_APPEND | O_WRITE)) {
        f.write(buffer, s);
        f.flush();
        f.close();
    }
    #endif
}
#endif

bool _sys_extendfile(char *fn, unsigned long fpos) {
    if (!_xtgvConfigAccess(fn, true)) return false;
    bool result = true;
    File f;

    _xtgvDiskActivity(true);
    f = SD.open(fn, O_WRITE | O_APPEND);
    if (f) {
        const unsigned long size = f.size();
        unsigned long remaining = fpos > size ? fpos - size : 0;
        const uint8 zeros[128] = {};
        while (remaining) {
            const size_t count = remaining < sizeof(zeros) ? remaining : sizeof(zeros);
            if (f.write(zeros, count) != count) {
                result = false;
                break;
            }
            remaining -= count;
        }
        f.close();
    } else {
        result = false;
    }
    _xtgvDiskActivity(false);
    return result;
}

uint8 _sys_readseq(uint8 *filename, long fpos) {
    uint8 result = 0xff;
    File f;
    uint8 bytesread;
    uint8 dmabuf[BlkSZ];
    uint8 i;

    _xtgvDiskActivity(true);
    f = SD.open((char *)filename, O_READ);
    if (f) {
        if (f.seek(fpos)) {
            for (i = 0; i < BlkSZ; ++i)
                dmabuf[i] = 0x1a;
            bytesread = f.read(&dmabuf[0], BlkSZ);
            if (bytesread) {
                for (i = 0; i < BlkSZ; ++i)
                    _RamWrite(dmaAddr + i, dmabuf[i]);
            }
            result = bytesread ? 0x00 : 0x01;
        } else {
            result = 0x01;
        }
        f.close();
    } else {
        result = 0x10;
    }
    _xtgvDiskActivity(false);
    return (result);
}

uint8 _sys_writeseq(uint8 *filename, long fpos) {
    uint8 result = 0xff;
    File f;

    _xtgvDiskActivity(true);
    if (_sys_extendfile((char *)filename, fpos))
        f = SD.open((char *)filename, O_RDWR);
    if (f) {
        if (f.seek(fpos)) {
            if (f.write(_RamSysAddr(dmaAddr), BlkSZ))
                result = 0x00;
        } else {
            result = 0x01;
        }
        f.close();
    } else {
        result = 0x10;
    }
    _xtgvDiskActivity(false);
    return (result);
}

uint8 _sys_readrand(uint8 *filename, long fpos) {
    uint8 result = 0xff;
    File f;
    uint8 bytesread;
    uint8 dmabuf[BlkSZ];
    uint8 i;
    long extSize;

    _xtgvDiskActivity(true);
    f = SD.open((char *)filename, O_READ);
    if (f) {
        if (f.seek(fpos)) {
            for (i = 0; i < BlkSZ; ++i)
                dmabuf[i] = 0x1a;
            bytesread = f.read(&dmabuf[0], BlkSZ);
            if (bytesread) {
                for (i = 0; i < BlkSZ; ++i)
                    _RamWrite(dmaAddr + i, dmabuf[i]);
            }
            result = bytesread ? 0x00 : 0x01;
        } else {
            if (fpos >= 65536L * BlkSZ) {
                result = 0x06; // seek past 8MB (largest file size in CP/M)
            } else {
                extSize = f.size();
                // round file size up to next full logical extent
                extSize = ExtSZ * ((extSize / ExtSZ) + ((extSize % ExtSZ) ? 1 : 0));
                if (fpos < extSize)
                    result = 0x01; // reading unwritten data
                else
                    result = 0x04; // seek to unwritten extent
            }
        }
        f.close();
    } else {
        result = 0x10;
    }
    _xtgvDiskActivity(false);
    return (result);
}

uint8 _sys_writerand(uint8 *filename, long fpos) {
    uint8 result = 0xff;
    File f;

    _xtgvDiskActivity(true);
    if (_sys_extendfile((char *)filename, fpos)) {
        f = SD.open((char *)filename, O_RDWR);
    }
    if (f) {
        if (f.seek(fpos)) {
            if (f.write(_RamSysAddr(dmaAddr), BlkSZ))
                result = 0x00;
        } else {
            result = 0x06;
        }
        f.close();
    } else {
        result = 0x10;
    }
    _xtgvDiskActivity(false);
    return (result);
}

static uint8 findNextDirName[13];
static uint16 fileRecords = 0;
static uint16 fileExtents = 0;
static uint16 fileExtentsUsed = 0;
static uint16 firstFreeAllocBlock;

uint8 _findnext(uint8 isdir) {
    File f;
    uint8 result = 0xff;
    bool isfile;
    uint32 bytes;

    _xtgvDiskActivity(true);
    if (allExtents && fileRecords) {
        _mockupDirEntry(0);
        result = 0;
    } else {
        while (f = userdir.openNextFile()) {
            f.getName((char *)&findNextDirName[0], 13);
            isfile = !f.isDirectory();
            bytes = f.size();
            f.dirEntry(&fileDirEntry);
            f.close();
            if (!isfile)
                continue;
            _HostnameToFCBname(findNextDirName, fcbname);
            if (match(fcbname, pattern)) {
                if (isdir) {
                    // account for host files that aren't multiples of the block size
                    // by rounding their bytes up to the next multiple of blocks
                    if (bytes & (BlkSZ - 1)) {
                        bytes = (bytes & ~(BlkSZ - 1)) + BlkSZ;
                    }
                    fileRecords = bytes / BlkSZ;
                    fileExtents = fileRecords / BlkEX + ((fileRecords & (BlkEX - 1)) ? 1 : 0);
                    fileExtentsUsed = 0;
                    firstFreeAllocBlock = firstBlockAfterDir;
                    _mockupDirEntry(0);
                } else {
                    fileRecords = 0;
                    fileExtents = 0;
                    fileExtentsUsed = 0;
                    firstFreeAllocBlock = firstBlockAfterDir;
                }
                _RamWrite(tmpFCB, filename[0] - '@');
                _HostnameToFCB(tmpFCB, findNextDirName);
                result = 0x00;
                break;
            }
        }
    }
    _xtgvDiskActivity(false);
    return (result);
}

uint8 _findfirst(uint8 isdir) {
    uint8 path[4] = {'?', FOLDERCHAR, '?', 0};
    path[0] = filename[0];
    path[2] = filename[2];
    if (userdir)
        userdir.close();
    userdir = SD.open((char *)path); // Set directory search to start from the first position
    _HostnameToFCBname(filename, pattern);
    fileRecords = 0;
    fileExtents = 0;
    fileExtentsUsed = 0;
    return (_findnext(isdir));
}

uint8 _findnextallusers(uint8 isdir) {
    uint8 result = 0xFF;
    char dirname[13];
    bool done = false;

    while (!done) {
        while (!userdir) {
            userdir = rootdir.openNextFile();
            if (!userdir) {
                done = true;
                break;
            }
            userdir.getName(dirname, sizeof dirname);
            if (userdir.isDirectory() && strlen(dirname) == 1 && isxdigit(dirname[0])) {
                currFindUser = dirname[0] <= '9' ? dirname[0] - '0' : toupper(dirname[0]) - 'A' + 10;
                break;
            }
            userdir.close();
        }
        if (userdir) {
            result = _findnext(isdir);
            if (result) {
                userdir.close();
            } else {
                done = true;
            }
        } else {
            result = 0xFF;
            done = true;
        }
    }
    return result;
}

uint8 _findfirstallusers(uint8 isdir) {
    uint8 path[2] = {'?', 0};

    path[0] = filename[0];
    if (rootdir)
        rootdir.close();
    if (userdir)
        userdir.close();
    rootdir = SD.open((char *)path); // Set directory search to start from the first position
    strcpy((char *)pattern, "???????????");
    if (!rootdir)
        return 0xFF;
    fileRecords = 0;
    fileExtents = 0;
    fileExtentsUsed = 0;
    return (_findnextallusers(isdir));
}

uint8 _Truncate(char *filename, uint8 rc) {
    if (!_xtgvConfigAccess(filename, true)) return 0;
    File f;
    int result = 0;

    _xtgvDiskActivity(true);
    f = SD.open((char *)filename, O_WRITE | O_APPEND);
    if (f) {
        if (f.truncate(rc * BlkSZ)) {
            f.close();
            result = 1;
        }
    }
    _xtgvDiskActivity(false);
    return (result);
}

void _MakeUserDir() {
    uint8 dFolder = cDrive + 'A';
    uint8 uFolder = toupper(tohex(userCode));

    uint8 path[4] = {dFolder, FOLDERCHAR, uFolder, 0};

    _xtgvDiskActivity(true);
    SD.mkdir((char *)path);
    _xtgvDiskActivity(false);
}

uint8 _sys_makedisk(uint8 drive) {
    uint8 result = 0;
    if (drive < 1 || drive > 16) {
        result = 0xff;
    } else {
        uint8 dFolder = drive + '@';
        uint8 disk[2] = {dFolder, 0};
        _xtgvDiskActivity(true);
        if (!SD.mkdir((char *)disk)) {
            result = 0xfe;
        } else {
            uint8 path[4] = {dFolder, FOLDERCHAR, '0', 0};
            SD.mkdir((char *)path);
        }
        _xtgvDiskActivity(false);
    }

    return (result);
}

/* Hardware abstraction functions */
/*===============================================================================*/
// Private RunCPM guest/host benchmark ports. BIOS and BDOS use interrupt
// handoff in this build, so ordinary guest I/O reaches here.
#include "src/terminal/benchmark_report.h"

static const uint8 XTGV_DRV_PORT = 0x7a;
static const uint8 XTGV_DRV_QUIET_CAP_PORT = 0x7e;
static const uint8 XTGV_CPU_PORT = 0x7b;
static const uint8 XTGV_TERM_PORT = 0x79;
static const uint8 XTGV_REBOOT_PORT = 0x7c;
static const uint8 XTGV_SYNC_PORT = 0x7d;
static const uint64 XTGV_CPU_TSTATES = 13015509ULL;
static const uint32 XTGV_CPU8X_LOOPS = 500000UL;
static const uint32 XTGV_DRV_BYTES = 32768UL;
static uint32 xtgvBenchmarkStartedAt = 0;
static bool xtgvBenchmarkRunning = false;
static bool xtgvCpuQuiet = false;
static bool xtgvCpu8x = false;
static uint8 xtgvDrvOperation = 0;
static bool xtgvDrv8x = false;
static bool xtgvDrvQuiet = false;
static bool xtgvTermReported = false;
static uint8 xtgvSyncStatus = 0;

void _putch(uint8 ch);

static void _xtgvHardwarePrint(const char *text) {
    while (*text)
        _putch((uint8)*text++);
}

// Native diagnostics remain outside RunCPM; this is only the guest I/O seam.
#include "hardware/arduino_7xtgv/cardmark.h"
#if XTGV_CARDMARK_AVAILABLE
static char xtgvCardmarkArgs[128];
static uint8 xtgvCardmarkLength = 0;
static bool xtgvCardmarkOverflow = false;
static bool _xtgvCardmarkCancelled() {
#if XTGV_TERMINAL
    return forcedWarmReturn().pending();
#else
    return false;
#endif
}
#endif

static void _xtgvCPUBegin(bool quiet, bool use8x) {
    xtgvCpuQuiet = quiet;
    xtgvCpu8x = use8x;
    if (use8x)
        _xtgvHardwarePrint(quiet ? "VTCPU8X quiet : " : "VTCPU8X active: ");
    else
        _xtgvHardwarePrint(quiet ? "VTCPU quiet : " : "VTCPU active: ");
#if XTGV_TERMINAL
    if (quiet)
        terminalPause(true);
#endif
    xtgvBenchmarkStartedAt = (uint32)micros();
    xtgvBenchmarkRunning = true;
}

static void _xtgvCPUEnd() {
    if (!xtgvBenchmarkRunning)
        return;

    const uint32 elapsedUs = (uint32)micros() - xtgvBenchmarkStartedAt;
    const bool wasQuiet = xtgvCpuQuiet;
    const bool was8x = xtgvCpu8x;
    xtgvBenchmarkRunning = false;
    xtgvCpuQuiet = false;
    xtgvCpu8x = false;

#if XTGV_TERMINAL
    if (wasQuiet)
        terminalPause(false);
#endif

    char buffer[96];
    if (was8x) {
        const uint64 mloopsHundredths =
            (XTGV_CPU8X_LOOPS * 100ULL + (elapsedUs / 2)) /
            (elapsedUs ? elapsedUs : 1);
        snprintf(buffer, sizeof(buffer),
                 "%lu loops in %lu us = %llu.%02llu Mloops/s\r\n",
                 (unsigned long)XTGV_CPU8X_LOOPS, (unsigned long)elapsedUs,
                 mloopsHundredths / 100ULL, mloopsHundredths % 100ULL);
    } else {
        const uint64 mhzHundredths =
            (XTGV_CPU_TSTATES * 100ULL + (elapsedUs / 2)) /
            (elapsedUs ? elapsedUs : 1);
        snprintf(buffer, sizeof(buffer),
                 "%llu T-states in %lu us = %llu.%02llu MHz\r\n",
                 XTGV_CPU_TSTATES, (unsigned long)elapsedUs,
                 mhzHundredths / 100ULL, mhzHundredths % 100ULL);
    }
    _xtgvHardwarePrint(buffer);
}

static void _xtgvDRVBegin(uint8 operation, bool use8x, bool quiet = false) {
    xtgvDrvOperation = operation;
    xtgvDrv8x = use8x;
    xtgvDrvQuiet = quiet;
#if XTGV_TERMINAL
    if (quiet)
        terminalPause(true);
#endif
    xtgvBenchmarkStartedAt = (uint32)micros();
    xtgvBenchmarkRunning = true;
}

static void _xtgvDRVEnd(uint8 operation) {
    if (!xtgvBenchmarkRunning || xtgvDrvOperation != operation)
        return;

    const uint32 elapsedUs = (uint32)micros() - xtgvBenchmarkStartedAt;
    const bool wasQuiet = xtgvDrvQuiet;
    const uint64 kibHundredths =
        (XTGV_DRV_BYTES * 100000000ULL + (uint64(elapsedUs) * 512ULL)) /
        (uint64(elapsedUs ? elapsedUs : 1) * 1024ULL);
    xtgvBenchmarkRunning = false;
    xtgvDrvOperation = 0;
    xtgvDrvQuiet = false;
#if XTGV_TERMINAL
    if (wasQuiet)
        terminalPause(false);
#endif

    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%s %s: %lu bytes in %lu us = %llu.%02llu KiB/s\r\n",
             xtgvDrv8x ? "VTDRV8X" : "VTDRV",
             operation == 1 ? (wasQuiet ? "quiet write" : "write") :
                              (wasQuiet ? "quiet read" : "read "),
             (unsigned long)XTGV_DRV_BYTES,
             (unsigned long)elapsedUs, kibHundredths / 100ULL,
             kibHundredths % 100ULL);
    xtgvDrv8x = false;
    _xtgvHardwarePrint(buffer);
}

void _HardwareInit(void) {
}

void _HardwareOut(const uint32 Port, const uint32 Value) {
    const uint8 port = Port & 0xff;
    const uint8 value = Value & 0xff;
#if XTGV_CARDMARK_AVAILABLE
    if (port == 0x78) {
        if (xtgvCardmarkLength < sizeof(xtgvCardmarkArgs) - 1)
            xtgvCardmarkArgs[xtgvCardmarkLength++] = char(value);
        else xtgvCardmarkOverflow = true;
        return;
    }
    if (port == 0x77) {
        if (value == 1) {
            xtgvCardmarkLength = 0;
            xtgvCardmarkOverflow = false;
        } else if (value == 2) {
            xtgvCardmarkArgs[xtgvCardmarkLength] = 0;
            if (xtgvCardmarkOverflow) _xtgvHardwarePrint(" CARDMARK: argument overflow\r\n");
            else xtgvCardmarkRun(xtgvCardmarkArgs, _xtgvHardwarePrint, _xtgvCardmarkCancelled);
        }
        return;
    }
#endif
    if (port == XTGV_CPU_PORT) {
        switch (value) {
            case 1: _xtgvCPUBegin(false, false); break;
            case 2: _xtgvCPUBegin(true, false); break;
            case 3: _xtgvCPUEnd(); break;
            case 4: _xtgvCPUBegin(false, true); break;
            case 5: _xtgvCPUBegin(true, true); break;
            case 6: _xtgvCPUEnd(); break;
        }
    } else if (port == XTGV_DRV_PORT) {
        switch (value) {
            case 1: _xtgvDRVBegin(1, false); break;
            case 2: _xtgvDRVEnd(1); break;
            case 3: _xtgvDRVBegin(2, false); break;
            case 4: _xtgvDRVEnd(2); break;
            case 5: _xtgvDRVBegin(1, true); break;
            case 6: _xtgvDRVEnd(1); break;
            case 7: _xtgvDRVBegin(2, true); break;
            case 8: _xtgvDRVEnd(2); break;
            case 9: _xtgvDRVBegin(2, false, true); break;
            case 10: _xtgvDRVEnd(2); break;
            case 11: _xtgvDRVBegin(1, false, true); break;
            case 12: _xtgvDRVEnd(1); break;
            case 0xff: {
                const bool wasQuiet = xtgvDrvQuiet;
                xtgvBenchmarkRunning = false;
                xtgvDrvOperation = 0;
                xtgvDrv8x = false;
                xtgvDrvQuiet = false;
#if XTGV_TERMINAL
                if (wasQuiet)
                    terminalPause(false);
#endif
                break;
            }
        }
    } else if (port == XTGV_TERM_PORT && value == 1) {
#if XTGV_TERMINAL
        xtgvTermReported = false;
        terminalTextBenchmarkRequest();
#endif
    } else if (port == XTGV_REBOOT_PORT && value == 1) {
#if XTGV_TERMINAL
        // Wait in the terminal task, so the lazy SD history save completes
        // before REBOOT.COM resets the ESP32-S3.
        terminalScheduleReboot();
#endif
    } else if (port == XTGV_SYNC_PORT && value == 1) {
#if XTGV_TERMINAL
        // SYNC/REFLASH must finish both pending settings and history writes
        // before a graphics stream or firmware reset can starve lazy polling.
        terminalHistoryForceSave();
        terminalConfigForceSave();
        const bool historySaved = terminalHistoryStoragePoll();
        const bool configSaved = terminalConfigStoragePoll();
        xtgvSyncStatus = historySaved && configSaved ? 1 : 0xff;
#else
        xtgvSyncStatus = 0xff;
#endif
    } else if (port == XTGV_TERM_PORT && value == 2) {
#if XTGV_TERMINAL
        if (terminalTextBenchmarkDone() && !xtgvTermReported) {
            char buffer[160];
            uint32_t elapsed = terminalTextBenchmarkElapsedUs();
            uint32_t bytes = terminalTextBenchmarkBytes();
            uint32_t chars = terminalTextBenchmarkChars();
            terminal_benchmark::report(buffer, sizeof(buffer), chars, bytes, elapsed);
            _xtgvHardwarePrint(buffer);
            xtgvTermReported = true;
        }
#endif
    }
}

uint32 _HardwareIn(const uint32 Port) {
    const uint8 port = Port & 0xff;
    if (port == 0x77) return XTGV_CARDMARK_AVAILABLE ? 'C' : 0;
    if (port == XTGV_CPU_PORT)
        return 'V';
    if (port == XTGV_DRV_PORT)
        return 'D';
    if (port == XTGV_DRV_QUIET_CAP_PORT)
        return 'B'; // Both quiet read and quiet write; older firmware returns Q.
    if (port == XTGV_TERM_PORT) {
#if XTGV_TERMINAL
        if (!terminalTextBenchmarkDone())
            return 0;
        return 'D';
#else
        return 0;
#endif
    }
    if (port == XTGV_SYNC_PORT)
        return xtgvSyncStatus;
    return 0;
}

/* Console abstraction functions */
/*===============================================================================*/

#if XTGV_TERMINAL
int _kbhit(void) { return virtualAvailable(); }
uint8 _getch(void) { return virtualRead(); }
uint8 _getche(void) {
    uint8 ch = _getch();
    if (forcedWarmReturn().pending()) return ch; // wake token is not guest text
    virtualWrite(ch);
    return ch;
}
void _putch(uint8 ch) { virtualWrite(ch); }
void _clrscr(void) {
    const char *clear = "\033[2J\033[H";
    while (*clear) virtualWrite(*clear++);
}
#else
int _kbhit(void) { return Serial.available(); }
uint8 _getch(void) {
    while (!Serial.available())
        delay(1);
    return uint8(Serial.read());
}
uint8 _getche(void) {
    uint8 ch = _getch();
    Serial.write(ch);
    return ch;
}
void _putch(uint8 ch) { Serial.write(ch); }
void _clrscr(void) { Serial.print("\033[2J\033[H"); }
#endif
#endif
