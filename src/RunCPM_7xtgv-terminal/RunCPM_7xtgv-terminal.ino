#include <Arduino.h>
#include "runcpm-config.h"

// Emit the selected board's ordinary function definitions once, in this TU.
#define XTGV_HAL_IMPLEMENTATION
#include "hardware/arduino_7xtgv/board.h"
// RunCPM - mixworx mods
#define BUILD  "7xtgv-terminal"

#if XTGV_TERMINAL
#include "src/terminal/terminal_runtime.h"
#include "src/terminal/display_policy.h"
#include "src/terminal/boot_warning.h"
#include "src/terminal/ini_text.h"
#endif

#include "hardware/arduino_7xtgv/runcpm_hooks.h"

#include "globals.h"

#if XTGV_TERMINAL
#include "src/terminal/terminal_persistence.h"
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
#include "cpu_mhz_coorect.h" // Corrected boot estimate; upstream copy stays intact.

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

#if XTGV_CCP_7XF && !defined(CCP_INTERNAL)
  #error "XTGV_CCP_7XF requires CCP_INTERNAL"
#endif
#ifdef CCP_INTERNAL
  #if XTGV_CCP_7XF
    #include "ccp7xf.h"
  #else
    #include "ccp.h"
  #endif
#endif


static void printBootBanner() {
    _clrscr();

    _puts("CP/M 2.2 Emulator v" VERSION " by Marcelo Dantas\r\n");
    _puts("Arduino read/write support by Krzysztof Klis\r\n");
    _puts("Arduino 7xtgv HAL by Petr Antos (@apws/@7alken)\r\n");
    xtgvBoardPrintBootExtra(_puts);
    _puts("Build:  "); _puts(BUILD); _puts(" / ");
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
    xtgvZ80estimateClock();
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

    _puts("\r\n");
}

static void runCPM() {
#if XTGV_TERMINAL
    terminalWaitReady();
#endif
    #ifdef DEBUGLOG
      _sys_deletefile((uint8 *)LogName);
    #endif


    const XtgvBoardStorageStatus storage = xtgvBoardStorageInit();
    const bool storageReady = storage.mediaReady;

#if XTGV_TERMINAL
    bool profileLoaded = false;
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

#if XTGV_TERMINAL
    // Always release startup blanking, including absent SD/INI fallback.
    // The display core waits for the actual themed frame, not just this flag.
    terminalBootConfigComplete();
#endif
    printBootBanner();
    if (!storageReady)
        xtgvBoardPrintStorageStatus(storage, _puts);
    // All INI/history handles have closed; retain the mount and change only
    // the SPI transaction setting. Serial-only builds keep the 20 MHz default.
#if XTGV_TERMINAL
    if (storageReady) {
        const uint8_t mhz = profileLoaded && terminalConfigText.valid()
                          ? terminalConfiguredSdSpiMHz() : 20;
        char clockMessage[96];
        const bool applied = xtgvBoardStorageSetMHz(mhz);
        if (!applied) {
            snprintf(clockMessage, sizeof(clockMessage),
                     "SD SPI request %u MHz NOT APPLIED: unsupported or busy\r\n", unsigned(mhz));
            _puts(clockMessage);
        }
    }
#endif

    if (storageReady)
    {
#if XTGV_TERMINAL
        terminalPrintBootModule(_puts);
#endif
#if !defined(XTGV_W535_SD_MMC)
        char storageMessage[80];
        // This is the applied Arduino request, not independently measured SCK.
        snprintf(storageMessage, sizeof(storageMessage),
                 "Storage: Arduino SdFat/SPI (%lu MHz)\r\n",
                 (unsigned long)(xtgvStorageSpi.requestedHz() / 1000000UL));
        _puts(storageMessage);
#else
        _puts("Storage: legacy Arduino SD_MMC\r\n");
#endif
#if XTGV_TERMINAL
        if (!profileLoaded)
            _puts("INI unavailable; using compiled defaults.\r\n");
        if (!terminalConfigText.valid()) {
            char cacheMessage[128];
            snprintf(cacheMessage, sizeof(cacheMessage),
                     "INI cache: %s (%lu bytes); autosave will retry.",
                     terminalConfigCacheFailure ? terminalConfigCacheFailure : "unavailable",
                     (unsigned long)terminalConfigCacheRequestedBytes);
            vt::writeBootWarning(_puts, cacheMessage, terminalConfigUsesAnsi());
        }
        if (profileLoaded) {
            if (profileErrors) {
                char status[48];
                snprintf(status, sizeof(status), "INI LOADED WITH ERRORS (%u)\r\n", profileErrors);
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
            }
        }
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
#if XTGV_TERMINAL
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
        #if XTGV_TERMINAL
            vt::writeBootWarning(_puts, "Unable to initialize SD card.", terminalConfigUsesAnsi());
            vt::writeBootWarning(_puts, "CPU halted.", terminalConfigUsesAnsi());
        #else
            _puts("Unable to initialize SD card.\r\nCPU halted.\r\n");
        #endif
    }
}

#include "hardware/arduino_7xtgv/runtime.h"
