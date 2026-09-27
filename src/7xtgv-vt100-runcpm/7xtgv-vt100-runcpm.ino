#include <Arduino.h>
#include "runcpm-config.h"
// RunCPM - mixworx mods
#define BUILD  "7xtgv-runcpm-v2"

#if !defined(RUNCPM_HEADLESS)
#include "src/terminal/terminal_runtime.h"
#include "src/terminal/display_policy.h"
#include "src/terminal/boot_warning.h"
#include "src/terminal/ini_text.h"
#endif

//TODO MIXWORX
// - make FLAT HAL (arduino) under hardware/arduino_7xtgv - to place there SINGLE include per board containing ALL HAL declarations
// - till now, RunCPM.ino contains still some weirdness related to SPI abstraction
// - till now RunCPM doesnt report SPI init status - its EASY to misinterpret JUST MISSING SDCARD as SPI init failure !!!
// - so add separate SPI INIT confirmation and separate SDCARD access OK on terminal, it HELPS a lot !!!
// - also FLAT placement of "file per board" under hardware/arduino_7xtgv simplifies overview of all supported boards
// - these HAL declarations were intentionally moved ON TOP as first include

//#include "hardware/pico/pico_sd_spi.h"                 // Raspberry Pi Pico - normal (LED = GPIO25)
//#include "hardware/pico/pico_w_sd_spi.h"               // Raspberry Pi Pico W(iFi)   (LED = GPIO64)
#if defined(RUNCPM_TARGET_JC8048W535)
#include "hardware/arduino_7xtgv/jc8048w535_sdmmc.h"
#elif defined(RUNCPM_TARGET_JC8048W550)
#include "hardware/arduino_7xtgv/jc8048w550_sd_spi.h"
#elif defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
#include "hardware/arduino_7xtgv/pico_rp2350_lcd_1_47_a_sd_spi.h"
#else
#include "hardware/arduino_7xtgv/pico_rp2350_touch_lcd_2_sd_spi.h" // WAVESHARE-rp2350-touch-lcd-2
#endif



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
static constexpr const char *TerminalConfigErrorPath = "A/0/7XTGVTRM.ERR";
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

// Accessed only by the RunCPM/storage owner. Runtime keeps the small parsed
// settings snapshot; this buffer preserves the complete human-authored file.
static vt::IniText terminalConfigText;
static bool terminalConfigGuestWriting = false;

static bool readTerminalConfigText(File &source) {
    const size_t size = source ? source.size() : 0;
    uint8_t *data = terminalConfigText.beginLoad(size);
    if (!data) return false;
    for (size_t offset = 0; offset < size;) {
        const size_t count = size - offset < 512 ? size - offset : 512;
        if (source.read(data + offset, count) != int(count)) return false;
        offset += count;
    }
    terminalConfigText.finishLoad();
    return true;
}

static bool loadTerminalConfigText() {
    File source = SD.open(TerminalConfigPath, FILE_READ);
    const bool loaded = (bool(source) || !SD.exists(TerminalConfigPath)) &&
                        readTerminalConfigText(source);
    if (source) source.close();
    if (!loaded) terminalConfigText.invalidate();
    return loaded;
}

static bool updateConfigSettings(vt::IniText &text, uint8_t level,
                                 uint8_t zoom, const char *displayId, uint8_t palette,
                                 uint8_t dirty) {
    char display[24], value[4];
    if (dirty & ConfigZoomDirty) {
        snprintf(display, sizeof(display), "display:%s", displayId);
        snprintf(value, sizeof(value), "%u", unsigned(zoom));
        if (!text.set(display, "view_zoom", value)) return false;
    }
    if (dirty & ConfigBacklightDirty) {
        snprintf(value, sizeof(value), "%u", unsigned(level));
        if (!text.set("global", "backlight_level", value)) return false;
    }
    if (dirty & ConfigPaletteDirty) {
        snprintf(value, sizeof(value), "%u", unsigned(palette));
        if (!text.set("global", "mode_palette", value)) return false;
    }
    return true;
}

static bool writeTerminalConfigText(File &target) {
    for (size_t offset = 0; offset < terminalConfigText.size();) {
        const size_t remaining = terminalConfigText.size() - offset;
        const size_t count = remaining < 512 ? remaining : 512;
        if (target.write(terminalConfigText.data() + offset, count) != count) return false;
        offset += count;
    }
    return target.write(uint8_t(0x1a)) == 1;
}

static bool verifyTerminalConfigText(File &source) {
    if (!source || source.size() != terminalConfigText.size() + 1) return false;
    uint8_t block[512];
    for (size_t offset = 0; offset < terminalConfigText.size();) {
        const size_t remaining = terminalConfigText.size() - offset;
        const size_t count = remaining < sizeof(block) ? remaining : sizeof(block);
        if (source.read(block, count) != int(count) ||
            memcmp(block, terminalConfigText.data() + offset, count)) return false;
        offset += count;
    }
    return source.read() == 0x1a;
}

// Called before guest access, including VTPUT/VTGET's ordinary BDOS calls.
// Flush before the first read/mutation; never save over an open guest writer.
static bool terminalConfigFileAccess(const char *path, bool writing) {
    if (!vt::IniText::samePath(path, TerminalConfigPath)) return true;
    if (!terminalConfigGuestWriting) {
        terminalConfigForceSave();
        if (!terminalConfigStoragePoll()) return false;
    }
    if (writing) {
        terminalConfigText.invalidate();
        terminalConfigGuestWriting = true;
    }
    return true;
}

static void terminalConfigFileClosed(const char *path) {
    if (!vt::IniText::samePath(path, TerminalConfigPath) || !terminalConfigGuestWriting) return;
    terminalConfigGuestWriting = false;
    loadTerminalConfigText(); // text only; live profiles still reload on reboot
}

static void terminalConfigGuestIdle() {
    // Also covers ERA, rename-based editors and programs which warm-boot
    // without closing their FCB. Never reload a partial transfer at a key wait.
    terminalConfigGuestWriting = false;
    if (!terminalConfigText.valid()) loadTerminalConfigText();
}

static bool writeTerminalConfigErrorReport() {
    // A stale report is worse than none: remove it after a clean parse too.
    if (SD.exists(TerminalConfigErrorPath))
        SD.remove(TerminalConfigErrorPath);
    const unsigned total = terminalConfigErrorCount();
    const uint8_t count = terminalConfigErrorLineCount();
    if (!total)
        return true;
    File report = SD.open(TerminalConfigErrorPath, O_CREAT | O_WRITE);
    if (!report)
        return false;
    char line[256];
    int length = total == count
        ? snprintf(line, sizeof(line), "7XTGVTRM.INI parse errors (%u)\r\n", total)
        : snprintf(line, sizeof(line), "7XTGVTRM.INI parse errors (%u; first %u shown)\r\n",
                   total, unsigned(count));
    bool written = length > 0 && size_t(length) < sizeof(line) &&
                   report.write(reinterpret_cast<const uint8_t *>(line), size_t(length)) == size_t(length);
    for (uint8_t index = 0; written && index < count; ++index) {
        uint16_t number = 0;
        const char *section = nullptr, *text = nullptr;
        if (!terminalConfigErrorLine(index, number, section, text)) {
            written = false;
            break;
        }
        length = snprintf(line, sizeof(line), "%4u: %s: %s\r\n", unsigned(number), section, text);
        written = length > 0 && size_t(length) < sizeof(line) &&
                  report.write(reinterpret_cast<const uint8_t *>(line), size_t(length)) == size_t(length);
    }
    written = written && report.write(uint8_t(0x1a)) == 1;
    report.flush();
    report.close();
    return written;
}

bool terminalConfigStoragePoll() {
    uint8_t level = 0, zoom = 0, palette = 0, dirty = 0;
    if (!terminalConfigSaveDue(level, zoom, &palette, &dirty))
        return true;
    if (terminalConfigGuestWriting)
        return false; // defer even explicit SYNC until guest close/CCP
    terminalPause(true);
    // A last key could have arrived between the due check and pause. Recheck
    // the settled snapshot now that its producing core cannot mutate it.
    if (!terminalConfigSaveDue(level, zoom, &palette, &dirty)) {
        terminalPause(false);
        return true;
    }
    terminalDiskActivity(true);
    bool saved = (terminalConfigText.valid() || loadTerminalConfigText()) &&
                 updateConfigSettings(terminalConfigText, level, zoom,
                                      vt::displayHalDefaults().id, palette, dirty);
    // Do not open/truncate any destination until the complete RAM edit exists.
    if (saved && SD.exists(TerminalConfigNewPath))
        saved = SD.remove(TerminalConfigNewPath);
    File target;
    if (saved) target = SD.open(TerminalConfigNewPath, O_CREAT | O_WRITE);
    saved = saved && bool(target) && writeTerminalConfigText(target);
    if (target) { target.flush(); target.close(); }
    if (saved) {
        File verification = SD.open(TerminalConfigNewPath, FILE_READ);
        saved = verifyTerminalConfigText(verification);
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

#include "abstraction_arduino_7xtgv.h"

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
        const bool cached = (bool(profileFile) || !SD.exists(TerminalConfigPath)) &&
                            readTerminalConfigText(profileFile);
        if (profileFile) {
            profileLoaded = true;
            terminalConfigBegin();
            if (cached) {
                for (size_t i = 0; i < terminalConfigText.size(); ++i)
                    terminalConfigFeed(terminalConfigText.data()[i]);
            } else if (profileFile.seek(0)) {
                // Low-memory/oversize fallback may still load the profile;
                // autosave must wait for a successful full cache load.
                while (profileFile.available())
                    terminalConfigFeed(uint8_t(profileFile.read()));
            }
            profileFile.close();
            profileErrors = terminalConfigEnd();
            mask8bit = terminalConfiguredConsoleBits() == 8 ? 0xff : 0x7f;
            writeTerminalConfigErrorReport();
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

#if !defined(RUNCPM_HEADLESS)
    // Always release startup blanking, including absent SD/INI fallback.
    // The display core waits for the actual themed frame, not just this flag.
    terminalBootConfigComplete();
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
            if (profileErrors) {
                char status[48];
                snprintf(status, sizeof(status), "LOADED WITH ERRORS (%u)\r\n", profileErrors);
                _puts(status);
                for (uint8_t index = 0; index < terminalConfigErrorLineCount(); ++index) {
                    uint16_t number = 0;
                    const char *section = nullptr, *text = nullptr;
                    if (!terminalConfigErrorLine(index, number, section, text))
                        continue;
                    char detail[256];
                    snprintf(detail, sizeof(detail), "%4u: %s: %s", unsigned(number), section, text);
                    if (terminalConfigUsesAnsi()) {
                        // Our light palette maps ANSI red paper to the pale error field.
                        _puts("\033[30;41m");
                        _puts(detail);
                        _puts("\033[K\033[0m\r\n");
                    } else {
                        _puts("*** "); _puts(detail); _puts("\r\n");
                    }
                }
            } else {
                _puts("OK\r\n");
            }
        } else {
            _puts("COMPILED DEFAULTS\r\n");
        }
        if (!terminalConfigText.valid())
            _puts("INI RAM cache unavailable; autosave will retry.\r\n");
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
#if !defined(RUNCPM_HEADLESS)
                    vt::writeBootWarning(_puts, vt::displayHalDefaults().bootWarning,
                                         terminalConfigUsesAnsi());
#endif
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
        terminalBootConfigComplete();
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
