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
#if defined ESP32
    #define HostOS 0x05
#endif
#if defined _STM32_DEF_
    #define HostOS 0x06
#endif
#if defined ARDUINO_ARCH_RP2040
    #define HostOS 0x07
#endif

static inline void _diskActivity(bool active);

/* Memory abstraction functions */
/*===============================================================================*/
uint16 _RamLoad(uint8 *filename, uint16 address, uint16 maxsize) {
    File f;
    uint16 bytesread = 0;

    _diskActivity(true);
    if (f = SD.open((char *)filename, FILE_READ)) {
        while (f.available()) {
            _RamWrite(address++, f.read());
            bytesread++;
            if (maxsize && bytesread >= maxsize)
                break;
        }
        f.close();
    }
    _diskActivity(false);
    return (bytesread);
}

// Boards without a dedicated activity LED leave LED undefined.
static inline void _diskActivity(bool active) {
#if !defined(RUNCPM_HEADLESS)
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

bool _sys_exists(uint8 *filename) {
    _diskActivity(true);
    const bool exists = SD.exists((const char *)filename);
    _diskActivity(false);
    return exists;
}

File _sys_fopen_w(uint8 *filename) {
    _diskActivity(true);
    File file = SD.open((char *)filename, O_CREAT | O_WRITE);
    _diskActivity(false);
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

    _diskActivity(true);
    if (f = SD.open((char *)disk, O_READ)) {
        if (f.isDirectory())
            result = TRUE;
        f.close();
    }
    _diskActivity(false);
    return (result);
}

long _sys_filesize(uint8 *filename) {
    long l = -1;
    File f;

    _diskActivity(true);
    if (f = SD.open((char *)filename, O_RDONLY)) {
        l = f.size();
        f.close();
    }
    _diskActivity(false);
    return (l);
}

int _sys_openfile(uint8 *filename) {
    File f;
    int result = 0;

    _diskActivity(true);
    f = SD.open((char *)filename, O_READ);
    if (f) {
        f.dirEntry(&fileDirEntry);
        f.close();
        result = 1;
    }
    _diskActivity(false);
    return (result);
}

int _sys_makefile(uint8 *filename) {
    File f;
    int result = 0;

    _diskActivity(true);
    f = SD.open((char *)filename, O_CREAT | O_WRITE);
    if (f) {
        f.close();
        result = 1;
    }
    _diskActivity(false);
    return (result);
}

int _sys_deletefile(uint8 *filename) {
    _diskActivity(true);
    const int result = SD.remove((char *)filename);
    _diskActivity(false);
    return result;
}

int _sys_renamefile(uint8 *filename, uint8 *newname) {
    File f;
    int result = 0;

    _diskActivity(true);
    f = SD.open((char *)filename, O_WRITE | O_APPEND);
    if (f) {
        if (f.rename((char *)newname)) {
            f.close();
            result = 1;
        }
    }
    _diskActivity(false);
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
    bool result = true;
    File f;

    _diskActivity(true);
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
    _diskActivity(false);
    return result;
}

uint8 _sys_readseq(uint8 *filename, long fpos) {
    uint8 result = 0xff;
    File f;
    uint8 bytesread;
    uint8 dmabuf[BlkSZ];
    uint8 i;

    _diskActivity(true);
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
    _diskActivity(false);
    return (result);
}

uint8 _sys_writeseq(uint8 *filename, long fpos) {
    uint8 result = 0xff;
    File f;

    _diskActivity(true);
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
    _diskActivity(false);
    return (result);
}

uint8 _sys_readrand(uint8 *filename, long fpos) {
    uint8 result = 0xff;
    File f;
    uint8 bytesread;
    uint8 dmabuf[BlkSZ];
    uint8 i;
    long extSize;

    _diskActivity(true);
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
    _diskActivity(false);
    return (result);
}

uint8 _sys_writerand(uint8 *filename, long fpos) {
    uint8 result = 0xff;
    File f;

    _diskActivity(true);
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
    _diskActivity(false);
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

    _diskActivity(true);
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
    _diskActivity(false);
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
    File f;
    int result = 0;

    _diskActivity(true);
    f = SD.open((char *)filename, O_WRITE | O_APPEND);
    if (f) {
        if (f.truncate(rc * BlkSZ)) {
            f.close();
            result = 1;
        }
    }
    _diskActivity(false);
    return (result);
}

void _MakeUserDir() {
    uint8 dFolder = cDrive + 'A';
    uint8 uFolder = toupper(tohex(userCode));

    uint8 path[4] = {dFolder, FOLDERCHAR, uFolder, 0};

    _diskActivity(true);
    SD.mkdir((char *)path);
    _diskActivity(false);
}

uint8 _sys_makedisk(uint8 drive) {
    uint8 result = 0;
    if (drive < 1 || drive > 16) {
        result = 0xff;
    } else {
        uint8 dFolder = drive + '@';
        uint8 disk[2] = {dFolder, 0};
        _diskActivity(true);
        if (!SD.mkdir((char *)disk)) {
            result = 0xfe;
        } else {
            uint8 path[4] = {dFolder, FOLDERCHAR, '0', 0};
            SD.mkdir((char *)path);
        }
        _diskActivity(false);
    }

    return (result);
}

/* Hardware abstraction functions */
/*===============================================================================*/
// Private RunCPM guest/host benchmark ports. BIOS and BDOS use interrupt
// handoff in this build, so ordinary guest I/O reaches here.
static const uint8 VTDRV_PORT = 0x7a;
static const uint8 VTCPU_PORT = 0x7b;
static const uint8 VTTERM_PORT = 0x79;
static const uint8 VTREBOOT_PORT = 0x7c;
static const uint8 VTSYNC_PORT = 0x7d;
static const uint64 VTCPU_TSTATES = 13015509ULL;
static const uint32 VTCPU8X_LOOPS = 500000UL;
static const uint32 VTDRV_BYTES = 32768UL;
static uint32 vtBenchmarkStartedAt = 0;
static bool vtBenchmarkRunning = false;
static bool vtcpuQuiet = false;
static bool vtcpu8x = false;
static uint8 vtdrvOperation = 0;
static bool vtdrv8x = false;
static bool vttermReported = false;
static uint8 vtSyncStatus = 0;

void _putch(uint8 ch);

static void _HardwarePrint(const char *text) {
    while (*text)
        _putch((uint8)*text++);
}

static void _VTCPUBegin(bool quiet, bool use8x) {
    vtcpuQuiet = quiet;
    vtcpu8x = use8x;
    if (use8x)
        _HardwarePrint(quiet ? "VTCPU8X quiet : " : "VTCPU8X active: ");
    else
        _HardwarePrint(quiet ? "VTCPU quiet : " : "VTCPU active: ");
#if !defined(RUNCPM_HEADLESS)
    if (quiet)
        terminalPause(true);
#endif
    vtBenchmarkStartedAt = (uint32)micros();
    vtBenchmarkRunning = true;
}

static void _VTCPUEnd() {
    if (!vtBenchmarkRunning)
        return;

    const uint32 elapsedUs = (uint32)micros() - vtBenchmarkStartedAt;
    const bool wasQuiet = vtcpuQuiet;
    const bool was8x = vtcpu8x;
    vtBenchmarkRunning = false;
    vtcpuQuiet = false;
    vtcpu8x = false;

#if !defined(RUNCPM_HEADLESS)
    if (wasQuiet)
        terminalPause(false);
#endif

    char buffer[96];
    if (was8x) {
        const uint64 mloopsHundredths =
            (VTCPU8X_LOOPS * 100ULL + (elapsedUs / 2)) /
            (elapsedUs ? elapsedUs : 1);
        snprintf(buffer, sizeof(buffer),
                 "%lu loops in %lu us = %llu.%02llu Mloops/s\r\n",
                 (unsigned long)VTCPU8X_LOOPS, (unsigned long)elapsedUs,
                 mloopsHundredths / 100ULL, mloopsHundredths % 100ULL);
    } else {
        const uint64 mhzHundredths =
            (VTCPU_TSTATES * 100ULL + (elapsedUs / 2)) /
            (elapsedUs ? elapsedUs : 1);
        snprintf(buffer, sizeof(buffer),
                 "%llu T-states in %lu us = %llu.%02llu MHz\r\n",
                 VTCPU_TSTATES, (unsigned long)elapsedUs,
                 mhzHundredths / 100ULL, mhzHundredths % 100ULL);
    }
    _HardwarePrint(buffer);
}

static void _VTDRVBegin(uint8 operation, bool use8x) {
    vtdrvOperation = operation;
    vtdrv8x = use8x;
    vtBenchmarkStartedAt = (uint32)micros();
    vtBenchmarkRunning = true;
}

static void _VTDRVEnd(uint8 operation) {
    if (!vtBenchmarkRunning || vtdrvOperation != operation)
        return;

    const uint32 elapsedUs = (uint32)micros() - vtBenchmarkStartedAt;
    const uint64 kibHundredths =
        (VTDRV_BYTES * 100000000ULL + (uint64(elapsedUs) * 512ULL)) /
        (uint64(elapsedUs ? elapsedUs : 1) * 1024ULL);
    vtBenchmarkRunning = false;
    vtdrvOperation = 0;

    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%s %s: %lu bytes in %lu us = %llu.%02llu KiB/s\r\n",
             vtdrv8x ? "VTDRV8X" : "VTDRV", operation == 1 ? "write" : "read ",
             (unsigned long)VTDRV_BYTES,
             (unsigned long)elapsedUs, kibHundredths / 100ULL,
             kibHundredths % 100ULL);
    vtdrv8x = false;
    _HardwarePrint(buffer);
}

void _HardwareInit(void) {
}

void _HardwareOut(const uint32 Port, const uint32 Value) {
    const uint8 port = Port & 0xff;
    const uint8 value = Value & 0xff;
    if (port == VTCPU_PORT) {
        switch (value) {
            case 1: _VTCPUBegin(false, false); break;
            case 2: _VTCPUBegin(true, false); break;
            case 3: _VTCPUEnd(); break;
            case 4: _VTCPUBegin(false, true); break;
            case 5: _VTCPUBegin(true, true); break;
            case 6: _VTCPUEnd(); break;
        }
    } else if (port == VTDRV_PORT) {
        switch (value) {
            case 1: _VTDRVBegin(1, false); break;
            case 2: _VTDRVEnd(1); break;
            case 3: _VTDRVBegin(2, false); break;
            case 4: _VTDRVEnd(2); break;
            case 5: _VTDRVBegin(1, true); break;
            case 6: _VTDRVEnd(1); break;
            case 7: _VTDRVBegin(2, true); break;
            case 8: _VTDRVEnd(2); break;
            case 0xff:
                vtBenchmarkRunning = false;
                vtdrvOperation = 0;
                vtdrv8x = false;
                break;
        }
    } else if (port == VTTERM_PORT && value == 1) {
#if !defined(RUNCPM_HEADLESS)
        vttermReported = false;
        terminalTextBenchmarkRequest();
#endif
    } else if (port == VTREBOOT_PORT && value == 1) {
#if !defined(RUNCPM_HEADLESS)
        // Wait in the terminal task, so the lazy SD history save completes
        // before REBOOT.COM resets the ESP32-S3.
        terminalScheduleReboot();
#endif
    } else if (port == VTSYNC_PORT && value == 1) {
#if !defined(RUNCPM_HEADLESS)
        // SYNC.COM must complete the verified history transaction before a
        // following long graphics stream can starve the normal input-wait poll.
        terminalHistoryForceSave();
        vtSyncStatus = terminalHistoryStoragePoll() ? 1 : 0xff;
#else
        vtSyncStatus = 0xff;
#endif
    } else if (port == VTTERM_PORT && value == 2) {
#if !defined(RUNCPM_HEADLESS)
        if (terminalTextBenchmarkDone() && !vttermReported) {
            char buffer[96];
            uint32_t elapsed = terminalTextBenchmarkElapsedUs();
            uint32_t bytes = terminalTextBenchmarkBytes();
            uint32_t chars = terminalTextBenchmarkChars();
            uint32_t rate = elapsed ? uint32_t((uint64_t(bytes) * 1000000ULL) / elapsed) : 0;
            uint32_t charRate = elapsed ? uint32_t((uint64_t(chars) * 1000000ULL) / elapsed) : 0;
            snprintf(buffer, sizeof(buffer), "VTTERM native: %lu chars in %lu us = %lu chars/s (%lu B/s)\r\n",
                     (unsigned long)chars, (unsigned long)elapsed, (unsigned long)charRate,
                     (unsigned long)rate);
            _HardwarePrint(buffer);
            vttermReported = true;
        }
#endif
    }
}

uint32 _HardwareIn(const uint32 Port) {
    const uint8 port = Port & 0xff;
    if (port == VTCPU_PORT)
        return 'V';
    if (port == VTDRV_PORT)
        return 'D';
    if (port == VTTERM_PORT) {
#if !defined(RUNCPM_HEADLESS)
        if (!terminalTextBenchmarkDone())
            return 0;
        return 'D';
#else
        return 0;
#endif
    }
    if (port == VTSYNC_PORT)
        return vtSyncStatus;
    return 0;
}

/* Console abstraction functions */
/*===============================================================================*/

#if defined(RUNCPM_HEADLESS)
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
#else
int _kbhit(void) { return virtualAvailable(); }
uint8 _getch(void) { return virtualRead(); }
uint8 _getche(void) {
    uint8 ch = _getch();
    virtualWrite(ch);
    return ch;
}
void _putch(uint8 ch) { virtualWrite(ch); }
void _clrscr(void) {
    const char *clear = "\033[2J\033[H";
    while (*clear) virtualWrite(*clear++);
}
#endif
#endif
