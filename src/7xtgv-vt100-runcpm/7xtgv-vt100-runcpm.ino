#include <Arduino.h>
#include "runcpm-config.h"
// RunCPM - mixworx mods
#define BUILD  "7xtgv-runcpm-v2"

#if !defined(RUNCPM_HEADLESS)
#include "terminal_runtime.h"
#include "display_policy.h"
#endif

//TODO MIXWORX
// - make FLAT HAL (arduino) instead of foldered "hardware" - to place there SINGLE include per board containing ALL HAL declarations
// - till now, RunCPM.ino contains still some weirdness related to SPI abstraction
// - till now RunCPM doesnt report SPI init status - its EASY to misinterpret JUST MISSING SDCARD as SPI init failure !!!
// - so add separate SPI INIT confirmation and separate SDCARD access OK on terminal, it HELPS a lot !!!
// - also FLAT placement of "file per board" under hal folder simplifies overview of all supported boards
// - these HAL declarations were intentionally moved ON TOP as first include

//#include "hardware/pico/pico_sd_spi.h"                 // Raspberry Pi Pico - normal (LED = GPIO25)
//#include "hardware/pico/pico_w_sd_spi.h"               // Raspberry Pi Pico W(iFi)   (LED = GPIO64)
#if defined(RUNCPM_TARGET_JC8048W535)
#include "hal/jc8048w535_sdmmc.h"
#elif defined(RUNCPM_TARGET_JC8048W550)
#include "hal/jc8048w550_sd_spi.h"
#else
#include "hal/pico_rp2350_touch_lcd_2_sd_spi.h"        // WAVESHARE-rp2350-touch-lcd-2
#endif
//#include "hal/pico_rp2350_lcd_1_47_sd_spi.h"           // WAVESHARE-rp2350-lcd-1-47



#if defined(RUNCPM_TARGET_JC8048W535) || defined(RUNCPM_TARGET_JC8048W550)
RunCPMStorage SD;
#else
#include <SPI.h>
#include <SdFat.h>              // Greinman SdFat from Library Manager
// #include <ESP8266SdFat.h>

SdFat32 SD; // 32-bit file sizes, matching RunCPM's File32 usage
typedef File32 File;
#endif

#include "globals.h"

#if !defined(RUNCPM_HEADLESS)
// Human-editable terminal history, separate from CP/M files but kept beside
// 7XTGVTRM.INI. A validated .NEW file is rotated into place while .BAK retains
// the prior generation, so an interrupted write never destroys the last copy.
static constexpr const char *TerminalHistoryPath = "A/0/7XTGVTRM.BUF";
static constexpr const char *TerminalHistoryNewPath = "A/0/7XTGVTRM.NEW";
static constexpr const char *TerminalHistoryBackupPath = "A/0/7XTGVTRM.BAK";
// Do not use 7XTGVTRM.NEW/.BAK here: FAT's 8.3 names would collide with the
// history transaction above. These belong solely to the INI transaction.
static constexpr const char *TerminalConfigPath = "A/0/7XTGVTRM.INI";
static constexpr const char *TerminalConfigNewPath = "A/0/7XTCFG.NEW";
static constexpr const char *TerminalConfigBackupPath = "A/0/7XTCFG.BAK";
static constexpr const char *TerminalHistoryV1Header = "# 7xtgv terminal history v1\n";
static constexpr const char *TerminalHistoryV2Header = "# 7xtgv terminal history v2\n";
static constexpr size_t TerminalHistoryV2FooterLength = 17; // # crc32=XXXXXXXX\n
static uint32_t terminalHistoryCRC32Byte(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320UL & uint32_t(-int32_t(crc & 1)));
    return crc;
}
static int terminalHistoryHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool terminalHistoryFileValid(File &file) {
    const size_t headerLength = strlen(TerminalHistoryV2Header);
    const size_t size = file.size();
    if (size < headerLength)
        return false;
    char header[32] = {};
    if (!file.seek(0) ||
        file.read(reinterpret_cast<uint8_t *>(header), headerLength) != headerLength)
        return false;
    // Existing v1 history remains readable. Every newly written v2 file must
    // have its completion marker, so a torn write is rejected.
    if (!memcmp(header, TerminalHistoryV1Header, headerLength))
        return file.seek(0);
    if (memcmp(header, TerminalHistoryV2Header, headerLength) ||
        size < headerLength + TerminalHistoryV2FooterLength)
        return false;
    char footer[TerminalHistoryV2FooterLength + 1] = {};
    if (!file.seek(size - TerminalHistoryV2FooterLength) ||
        file.read(reinterpret_cast<uint8_t *>(footer), TerminalHistoryV2FooterLength) !=
            TerminalHistoryV2FooterLength ||
        memcmp(footer, "# crc32=", 8) || footer[16] != '\n')
        return false;
    uint32_t expected = 0;
    for (uint8_t i = 8; i < 16; ++i) {
        const int digit = terminalHistoryHex(footer[i]);
        if (digit < 0)
            return false;
        expected = (expected << 4) | uint32_t(digit);
    }
    if (!file.seek(0))
        return false;
    uint32_t crc = 0xffffffffUL;
    for (size_t remaining = size - TerminalHistoryV2FooterLength; remaining; --remaining) {
        const int byte = file.read();
        if (byte < 0)
            return false;
        crc = terminalHistoryCRC32Byte(crc, uint8_t(byte));
    }
    if ((crc ^ 0xffffffffUL) != expected)
        return false;
    return file.seek(0);
}

static File openTerminalHistory() {
    const char *paths[] = {TerminalHistoryPath, TerminalHistoryNewPath,
                           TerminalHistoryBackupPath};
    for (const char *path : paths) {
        if (!SD.exists(path))
            continue;
        File file = SD.open(path, FILE_READ);
        if (file && terminalHistoryFileValid(file))
            return file;
        if (file)
            file.close();
    }
    return File();
}

static bool writeTerminalHistoryByte(void *context, uint8_t byte) {
    return static_cast<File *>(context)->write(byte) == 1;
}
bool terminalHistoryStoragePoll() {
    if (!terminalHistorySaveDue())
        return true;
    // Stop history mutation while this owner core serialises it. Filesystem
    // operations themselves never leave the RunCPM core.
    terminalPause(true);
    terminalDiskActivity(true);
    if (SD.exists(TerminalHistoryNewPath))
        SD.remove(TerminalHistoryNewPath);
    File file = SD.open(TerminalHistoryNewPath, O_CREAT | O_WRITE);
    bool saved = bool(file) && terminalHistoryWrite(writeTerminalHistoryByte, &file);
    if (file) {
        file.flush();
        file.close();
    }
    if (saved) {
        File verification = SD.open(TerminalHistoryNewPath, FILE_READ);
        saved = bool(verification) && terminalHistoryFileValid(verification);
        if (verification)
            verification.close();
    }
    if (saved) {
        if (SD.exists(TerminalHistoryBackupPath))
            SD.remove(TerminalHistoryBackupPath);
        const bool hadPrevious = SD.exists(TerminalHistoryPath);
        const bool backedUp = !hadPrevious ||
                              SD.rename(TerminalHistoryPath, TerminalHistoryBackupPath);
        saved = backedUp && SD.rename(TerminalHistoryNewPath, TerminalHistoryPath);
        if (saved && SD.exists(TerminalHistoryBackupPath))
            SD.remove(TerminalHistoryBackupPath);
        else if (hadPrevious && backedUp && !SD.exists(TerminalHistoryPath))
            SD.rename(TerminalHistoryBackupPath, TerminalHistoryPath);
    }
    terminalPause(false);
    // Refresh the visible pulse after the paused display task resumes. A slow
    // verified write can otherwise consume the entire 75 ms latch unseen.
    terminalDiskActivity(true);
    terminalHistorySaveComplete(saved);
    return saved;
}

static bool configEqualNoCase(const char *left, const char *right) {
    while (*left && *right) {
        char a = *left++, b = *right++;
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
        if (a != b) return false;
    }
    return !*left && !*right;
}

static bool configLineIsKey(const char *line, const char *key) {
    while (*line == ' ' || *line == '\t') ++line;
    while (*key) {
        if (!*line) return false;
        char a = *line++, b = *key++;
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (a != b) return false;
    }
    while (*line == ' ' || *line == '\t') ++line;
    return *line == '=';
}

static bool writeConfigSettings(File &source, File &target, uint8_t level,
                                uint8_t zoom, const char *displayId) {
    char line[192];
    size_t length = 0;
    bool valid = true;
    bool overlong = false, skippedCR = false;
    bool logicalEof = false;
    bool wroteZoom = false, wroteBacklight = false;
    size_t bytesRead = 0;
    enum Section : uint8_t { Other, Global, Display } section = Other;
    char displaySection[24];
    snprintf(displaySection, sizeof(displaySection), "[display:%s]", displayId);
    auto flushLine = [&](int ending) {
        line[length] = 0;
        const char *text = line;
        while (*text == ' ' || *text == '\t') ++text;
        size_t textLength = strlen(text);
        while (textLength && (text[textLength - 1] == ' ' || text[textLength - 1] == '\t'))
            --textLength;
        if (!overlong && *text == '[' && textLength < sizeof(line)) {
            char heading[sizeof(line)];
            memcpy(heading, text, textLength);
            heading[textLength] = 0;
            section = configEqualNoCase(heading, "[global]") ? Global :
                      configEqualNoCase(heading, displaySection) ? Display : Other;
        }
        const bool backlightKey = !overlong && section == Global &&
                                  configLineIsKey(line, "backlight_level");
        const bool zoomKey = !overlong && section == Display &&
                             configLineIsKey(line, "view_zoom");
        const bool duplicate = (backlightKey && wroteBacklight) || (zoomKey && wroteZoom);
        if ((backlightKey || zoomKey) && !duplicate) {
            char replacement[48];
            const int count = backlightKey
                ? snprintf(replacement, sizeof(replacement), "backlight_level=%u", unsigned(level))
                : snprintf(replacement, sizeof(replacement), "view_zoom=%u", unsigned(zoom));
            valid = valid && count > 0 && size_t(count) < sizeof(replacement) &&
                    target.write(reinterpret_cast<const uint8_t *>(replacement), size_t(count)) == size_t(count);
            if (ending >= 0) valid = valid && target.write(uint8_t(ending)) == 1;
            if (backlightKey) wroteBacklight = true;
            else wroteZoom = true;
        } else if (!duplicate) {
            valid = valid && target.write(reinterpret_cast<const uint8_t *>(line), length) == length;
            if (ending >= 0) valid = valid && target.write(uint8_t(ending)) == 1;
        }
        length = 0;
        overlong = false;
        return duplicate;
    };
    while (source.available() && valid) {
        const int byte = source.read();
        if (byte < 0) { valid = false; break; }
        ++bytesRead;
        if (byte == 0x1a) { logicalEof = true; break; }
        if (byte == '\n' && skippedCR) { skippedCR = false; continue; }
        if (byte == '\r' || byte == '\n') {
            skippedCR = flushLine(byte) && byte == '\r';
        } else if (length + 1 < sizeof(line)) {
            skippedCR = false;
            line[length++] = char(byte);
        } else {
            // Existing INI lines are parser-limited to this size. Preserve an
            // unexpected overlong line rather than truncating it silently.
            valid = valid && target.write(reinterpret_cast<const uint8_t *>(line), length) == length &&
                    target.write(uint8_t(byte)) == 1;
            length = 0;
            overlong = true;
            skippedCR = false;
        }
    }
    if (source && !logicalEof && bytesRead != source.size())
        valid = false;
    if (valid && length) flushLine(-1);
    if (!wroteZoom) {
        char saved[64];
        const int count = snprintf(saved, sizeof(saved),
                                   "\r\n[display:%s]\r\nview_zoom=%u\r\n",
                                   displayId, unsigned(zoom));
        valid = valid && count > 0 && size_t(count) < sizeof(saved) &&
                target.write(reinterpret_cast<const uint8_t *>(saved), size_t(count)) == size_t(count);
    }
    if (!wroteBacklight) {
        char saved[48];
        const int count = snprintf(saved, sizeof(saved),
                                   "\r\n[global]\r\nbacklight_level=%u\r\n", unsigned(level));
        valid = valid && count > 0 && size_t(count) < sizeof(saved) &&
                target.write(reinterpret_cast<const uint8_t *>(saved), size_t(count)) == size_t(count);
    }
    return valid && target.write(uint8_t(0x1a)) == 1;
}

bool terminalConfigStoragePoll() {
    uint8_t level = 0, zoom = 0;
    if (!terminalConfigSaveDue(level, zoom))
        return true;
    terminalPause(true);
    // A last key could have arrived between the due check and pause. Recheck
    // the settled snapshot now that its producing core cannot mutate it.
    if (!terminalConfigSaveDue(level, zoom)) {
        terminalPause(false);
        return true;
    }
    terminalDiskActivity(true);
    if (SD.exists(TerminalConfigNewPath))
        SD.remove(TerminalConfigNewPath);
    File source = SD.open(TerminalConfigPath, FILE_READ);
    File target = SD.open(TerminalConfigNewPath, O_CREAT | O_WRITE);
    bool saved = bool(target) && (!SD.exists(TerminalConfigPath) || bool(source)) &&
                 writeConfigSettings(source, target, level, zoom, vt::displayHalDefaults().id);
    if (source) source.close();
    if (target) { target.flush(); target.close(); }
    if (saved) {
        File verification = SD.open(TerminalConfigNewPath, FILE_READ);
        saved = bool(verification) && verification.size() > 0 &&
                verification.seek(verification.size() - 1) && verification.read() == 0x1a;
        if (verification) verification.close();
    }
    if (saved) {
        if (SD.exists(TerminalConfigBackupPath))
            SD.remove(TerminalConfigBackupPath);
        const bool hadPrevious = SD.exists(TerminalConfigPath);
        const bool backedUp = !hadPrevious || SD.rename(TerminalConfigPath, TerminalConfigBackupPath);
        saved = backedUp && SD.rename(TerminalConfigNewPath, TerminalConfigPath);
        if (saved && SD.exists(TerminalConfigBackupPath))
            SD.remove(TerminalConfigBackupPath);
        else if (hadPrevious && backedUp && !SD.exists(TerminalConfigPath))
            SD.rename(TerminalConfigBackupPath, TerminalConfigPath);
    }
    terminalConfigSaveComplete(saved);
    terminalPause(false);
    terminalDiskActivity(true);
    return saved;
}
#endif

// =========================================================================================
// Delays for LED blinking
// =========================================================================================
#define sDELAY  50
#define DELAY  100

#include "abstraction_arduino.h"

// =========================================================================================
// Serial port speed
// =========================================================================================
#define SERIALSPD 115200


// =========================================================================================
// PUN: device configuration
// =========================================================================================
#ifdef USE_PUN
  File pun_dev;
  int pun_open = FALSE;
#endif

// =========================================================================================
// LST: device configuration
// =========================================================================================
#ifdef USE_LST
  File lst_dev;
  int lst_open = FALSE;
#endif

#include "ram.h"
#include "console.h"
#include CPU          // CPU Type is now selected at the start of globals.h
#include "disk.h"
#include "host.h"
#include "cpm.h"

// Target-aware builds inject a fresh stamp even when Arduino reuses the
// sketch object. Direct Arduino builds fall back to the sketch compile time.
#ifdef XTGV_BUILD_STAMP
#define XTGV_STRINGIZE_INNER(value) #value
#define XTGV_STRINGIZE(value) XTGV_STRINGIZE_INNER(value)
#else
// __DATE__ is always "Mmm dd yyyy".  Project-facing timestamps deliberately
// use the compact YYMMDD form, including a leading zero for single-digit days.
static void printBuildDate() {
    const char *date = __DATE__;
    _putcon(date[9]); _putcon(date[10]);
    const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int month = 0;
    while (month < 12 && (date[0] != months[month * 3] ||
                          date[1] != months[month * 3 + 1] ||
                          date[2] != months[month * 3 + 2]))
        ++month;
    _putcon('0' + (month + 1) / 10);
    _putcon('0' + (month + 1) % 10);
    _putcon(date[4] == ' ' ? '0' : date[4]); _putcon(date[5]);
}
#endif

#ifdef CCP_INTERNAL
  #include "ccp.h"
#endif


static void printBootBanner() {
    _clrscr();

    _puts("CP/M 2.2 Emulator v" VERSION " by Marcelo Dantas\r\n");
    _puts("Arduino read/write support by Krzysztof Klis\r\n");
    _puts("HAL tweaks by 7alken\r\n");
    //TODO HAL/BOARD defined section here //_puts("arduino-pico suport by Guido .........\r\n");
    //TODO HAL/BOARD defined section here //_puts("WaveShare RP2350-TOUCH-LCD-2 by 7alken\r\n");
    _puts("Build "); _puts(BUILD); _puts(" / ");
#ifdef XTGV_BUILD_STAMP
    _puts(XTGV_STRINGIZE(XTGV_BUILD_STAMP));
#else
    printBuildDate();
    _putcon('-'); _putcon(__TIME__[0]); _putcon(__TIME__[1]);
    _putcon(__TIME__[3]); _putcon(__TIME__[4]);
#endif
    _puts("\r\n");

    _puts("------------------------------------------------\r\n");
    _puts("CPU is "); _puts(CPU_IS); _puts("\r\n");
    _puts("Run VTCPU.COM for active/quiet Z80 speed\r\n");
    _puts("BIOS at 0x"); _puthex16(BIOSjmppage);  _puts(" - ");
    _puts("BDOS at 0x"); _puthex16(BDOSjmppage);  _puts("\r\n");
    _puts("CCP " CCPname " at 0x"); _puthex16(CCPaddr); _puts("\r\n");

    #ifdef ABDOS
      _puts("     (A)BDOS.SYS by Pavel Zampach\r\n");
    #endif
    _puts("------------------------------------------------\r\n");
    #ifdef INT_HANDOFF
        _puts("BIOS/BDOS     method interrupt    handoff\r\n");
    #else
        _puts("BIOS/BDOS     method legacy       IN/OUT call\r\n");
    #endif
    #if BANKS > 1
      _puts("Banked Memory        "); _puthex8(BANKS); _puts(" banks\r\n");
    #else
      _puts("Banked Memory        "); _puthex8(BANKS);  _puts(" bank\r\n");
    #endif

    //TODO HAL/BOARD defined section here
    //Serial.printf("Free Memory          [\e[1m%d bytes\e[0m]\r\n", freeMemory());
    //_puts("Pico2 CPU Clock      300Mhz\r\n"); // Pico2 300Mhz

    _puts("\r\n");
}

static void runCPM() {
#if !defined(RUNCPM_HEADLESS)
    terminalWaitReady();
#endif
    #ifdef DEBUGLOG
      _sys_deletefile((uint8 *)LogName);
    #endif


//TODO HAL/BOARD defined section here
// =========================================================================================
// SPIINIT !! ONLY !! for ESP32-Boards = #define board_esp32
// =========================================================================================

#if !defined(RUNCPM_TARGET_JC8048W535) && !defined(RUNCPM_TARGET_JC8048W550)
    // Conservative module/card-connector setting. SPI1 is dedicated to SD;
    // the TFT uses a separate PIO-backed bus.
    #define SDMHZ RUNCPM_SD_MHZ
    #define SDMHZ_TXT RUNCPM_SD_MHZ_TEXT
    // This follows the RP2040/RP2350 SdFat guidance noted at
    // https://www.pschatzmann.ch/home/2021/03/14/rasperry-pico-with-the-sdfat-library/

    #define SD_CONFIG SdSpiConfig(SD_CS, DEDICATED_SPI, SD_SCK_MHZ(SDMHZ), &SPIBUS)
    // #define SD_CONFIG SdSpiConfig(SD_CS, DEDICATED_SPI, SPI_FULL_SPEED, &SPI)
    // #define SD_CONFIG SdSpiConfig(SD_CS, DEDICATED_SPI, SPI_QUARTER_SPEED, &SPI) //breadboard
    // #define SD_CONFIG SdSpiConfig(SD_CS, DEDICATED_SPI, SPI_HALF_SPEED, &SPI)    //breadboard

// =========================================================================================

//TODO HAL/BOARD defined section here
    SPIBUS.setSCK(SD_SCK);
    SPIBUS.setTX(SD_MOSI);
    SPIBUS.setRX(SD_MISO);

    //TODO //FIXME //MIXWORX
    // !!! CRITICAL to handle SEPARATE SPI init reporting here !!!
    // !!! CRITICAL to handle SEPAATE SDFAT init reporting here !!!

    //FIXME - this is now SINGLE confusing path, fail of SPI and CARD-presence mismatch !!!
    const bool storageReady = SD.begin(SD_CONFIG);
#else
    const bool pinsReady = SD.setPins();
    const bool storageReady = pinsReady && SD.begin();
#endif

#if !defined(RUNCPM_HEADLESS)
    bool profileLoaded = false, historyLoaded = false;
    unsigned profileErrors = 0;
    if (storageReady) {
        // Load the shared-card settings before any boot text is painted. The
        // SD-owner core parses them while the terminal core is paused.
        if (!SD.exists(TerminalConfigPath) && SD.exists(TerminalConfigBackupPath))
            SD.rename(TerminalConfigBackupPath, TerminalConfigPath);
        terminalPause(true);
        File profileFile = SD.open(TerminalConfigPath, FILE_READ);
        if (profileFile) {
            profileLoaded = true;
            terminalConfigBegin();
            while (profileFile.available())
                terminalConfigFeed(uint8_t(profileFile.read()));
            profileFile.close();
            profileErrors = terminalConfigEnd();
            mask8bit = terminalConfiguredConsoleBits() == 8 ? 0xff : 0x7f;
        }
        File historyFile = openTerminalHistory();
        if (historyFile) {
            historyLoaded = true;
            terminalHistoryLoadBegin();
            while (historyFile.available())
                terminalHistoryLoadFeed(uint8_t(historyFile.read()));
            historyFile.close();
            terminalHistoryLoadEnd();
        }
        terminalPause(false);
        if (profileLoaded)
            terminalWaitDisplayProfileApplied();
    }
#endif

    printBootBanner();
#if !defined(RUNCPM_TARGET_JC8048W535) && !defined(RUNCPM_TARGET_JC8048W550)
    _puts("Init SD SPI bus ......CONFIGURED\r\n");
    _puts("Mount MicroSD card ...");
#else
    #if defined(RUNCPM_TARGET_JC8048W535)
    _puts("Init SD_MMC pins .....");
    #else
    _puts("Init SD SPI bus ......");
    #endif
    _puts(pinsReady ? "OK\r\n" : "FAILED\r\n");
    _puts("Mount MicroSD card ...");
#endif

    if (storageReady)
    {
#if defined(RUNCPM_TARGET_JC8048W535)
        _puts("OK (1-bit, CLK=12 CMD=11 D0=13)\r\n");
#if defined(RUNCPM_HEADLESS)
        SD.printRoot(Serial);
#endif
#elif defined(RUNCPM_TARGET_JC8048W550)
        char sdInfo[80];
        snprintf(sdInfo, sizeof(sdInfo),
                 "OK (SPI, CS=10 MOSI=11 MISO=13 SCK=12, %lu Hz)\r\n",
                 (unsigned long)RUNCPM_W550_SD_EFFECTIVE_HZ);
        _puts(sdInfo);
#else
        _puts(SDMHZ_TXT); _puts(" Mhz\r\n");
#endif
#if !defined(RUNCPM_HEADLESS)
        _puts("Terminal profile .....");
        if (profileLoaded) {
            _puts(profileErrors ? "LOADED WITH ERRORS\r\n" : "OK\r\n");
        } else {
            _puts("COMPILED DEFAULTS\r\n");
        }
        _puts("Terminal history .....");
        _puts(historyLoaded ? "OK\r\n" : "EMPTY\r\n");
#endif
        _puts("-------------------------------------------------");


        if (VersionCCP >= 0x10 || SD.exists(CCPname))
        {
            #ifdef ABDOS
              _PatchBIOS();
            #endif

            // A COM program normally terminates through BDOS 0, which is a
            // CP/M warm boot.  Keep its CCP session and SUBMIT stream intact;
            // the version header is useful once at a real RunCPM start, not
            // between every command in that session.
            bool firstCCPStart = true;
            while (true)
            {
                if (firstCCPStart) {
                    _puts(CCPHEAD);
                    firstCCPStart = false;
                }
                _PatchCPM();
                Status = STATUS_RUNNING;

                #ifdef CCP_INTERNAL
                    _ccp();
                #else
                    if (!_RamLoad((uint8 *)CCPname, CCPaddr, 0))
                    {
                      _puts("Unable to load the CCP.\r\nCPU halted.\r\n");
                      break;
                    }
                    // Loads an autoexec file if it exists and this is the first boot
                    // The file contents are loaded at ccpAddr+8 up to 126 bytes then the size loaded is stored at ccpAddr+7
                    if (firstBoot)
                    {
                      if (_sys_exists((uint8*)AUTOEXEC))
                      {
                        uint16 cmd = CCPaddr + 8;
                        uint8 bytesread = (uint8)_RamLoad((uint8*)AUTOEXEC, cmd, 125);
                        uint8 blen = 0;
                        while (blen < bytesread && _RamRead(cmd + blen) > 31)
                          blen++;
                        _RamWrite(cmd + blen, 0x00);
                        _RamWrite(--cmd, blen);
                      }
                      if (BOOTONLY)
                        firstBoot = FALSE;
                    }
                    Z80reset();
                    SET_LOW_REGISTER(BC, _RamRead(DSKByte));
                    PC = CCPaddr;
                    Z80run(cpuDelayInstructions);
                #endif

                if (Status == STATUS_EXIT)

                #ifdef DEBUG
                  #ifdef DEBUGONHALT
                      Debug = 1;
                      Z80debug();
                  #endif
                #endif

                break;

                #ifdef USE_PUN
                    if (pun_dev) _sys_fflush(pun_dev);
                #endif

                #ifdef USE_LST
                    if (lst_dev) _sys_fflush(lst_dev);
                #endif
            }
        }
        else
        {
            _puts("Unable to load CP/M CCP.\r\nCPU halted.\r\n");
        }
    }
    else
    {
      _puts("Unable to initialize SD card.\r\nCPU halted.\r\n");
    }
}

#if defined(RUNCPM_HEADLESS)
void setup() {
    Serial.begin(SERIALSPD);
    while (!Serial)
        delay(10);
    static_assert(sizeof(uint8) == 1 && sizeof(uint16) == 2 && sizeof(uint32) == 4,
                  "RunCPM requires 8/16/32-bit integer types");
    runCPM();
}
void loop() { delay(10); }
#elif defined(RUNCPM_TARGET_JC8048W535) || defined(RUNCPM_TARGET_JC8048W550)
static TaskHandle_t runCPMTaskHandle = nullptr;
static TaskHandle_t runCPMIdleWindowTaskHandle = nullptr;

static void runCPMTask(void *) {
    runCPM();
    // A missing SD card legitimately makes runCPM() return. Do not leave the
    // watchdog-yield helper holding a deleted FreeRTOS task handle.
    runCPMTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

static void runCPMIdleWindowTask(void *) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (!runCPMTaskHandle) {
            runCPMIdleWindowTaskHandle = nullptr;
            vTaskDelete(nullptr);
        }
        // A one-tick suspension guarantees time for core 0's watched idle
        // task without adding work to the Z80 instruction dispatch path.
        vTaskSuspend(runCPMTaskHandle);
        vTaskDelay(1);
        vTaskResume(runCPMTaskHandle);
    }
}

void setup() {
    static_assert(sizeof(uint8) == 1 && sizeof(uint16) == 2 && sizeof(uint32) == 4,
                  "RunCPM requires 8/16/32-bit integer types");
    terminalSetup();
    // ESP-IDF measures task stack depth in bytes. RunCPM owns this task and
    // SD_MMC; Arduino's loopTask retains terminal/display/USB ownership.
    constexpr uint32_t RunCPMStackBytes = 16 * 1024;
    BaseType_t created = xTaskCreatePinnedToCore(runCPMTask, "RunCPM", RunCPMStackBytes, nullptr,
                                                1, &runCPMTaskHandle, 0);
    if (created != pdPASS) {
        const char *message = "RunCPM task creation failed.\r\n";
        while (*message)
            virtualWrite(*message++);
        return;
    }
    created = xTaskCreatePinnedToCore(runCPMIdleWindowTask, "RunCPMIdle", 2048, nullptr, 2,
                                     &runCPMIdleWindowTaskHandle, 0);
    if (created != pdPASS) {
        const char *message = "RunCPM idle-window task creation failed.\r\n";
        while (*message)
            virtualWrite(*message++);
    }
}
void loop() { terminalLoop(); }
#else
bool core1_separate_stack = true;
void setup() { terminalSetup(); }
void loop() { terminalLoop(); }
void setup1() { runCPM(); }
void loop1() { delay(1); }
#endif
