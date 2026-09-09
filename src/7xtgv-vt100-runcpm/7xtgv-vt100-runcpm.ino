#include <Arduino.h>
#include "runcpm-config.h"
// RunCPM - mixworx mods
#define BUILD  "7xtgv-runcpm-v2"

#if !defined(RUNCPM_HEADLESS)
#include "terminal_runtime.h"
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
#else
#include "hal/pico_rp2350_touch_lcd_2_sd_spi.h"        // WAVESHARE-rp2350-touch-lcd-2
#endif
//#include "hal/pico_rp2350_lcd_1_47_sd_spi.h"           // WAVESHARE-rp2350-lcd-1-47



#if defined(RUNCPM_TARGET_JC8048W535)
RunCPMStorage SD;
#else
#include <SPI.h>
#include <SdFat.h>              // Greinman SdFat from Library Manager
// #include <ESP8266SdFat.h>

SdFat32 SD; // 32-bit file sizes, matching RunCPM's File32 usage
typedef File32 File;
#endif

#include "globals.h"

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

#ifdef CCP_INTERNAL
  #include "ccp.h"
#endif


static void runCPM() {
#if !defined(RUNCPM_HEADLESS)
    terminalWaitReady();
#endif
    #ifdef DEBUGLOG
      _sys_deletefile((uint8 *)LogName);
    #endif

// =========================================================================================
// Printing the Startup-Messages
// =========================================================================================

    _clrscr();

    _puts("CP/M 2.2 Emulator v" VERSION " by Marcelo Dantas\r\n");
    _puts("Arduino read/write support by Krzysztof Klis\r\n");
    _puts("HAL tweaks by 7alken\r\n");
    //TODO HAL/BOARD defined section here //_puts("arduino-pico suport by Guido .........\r\n");
    //TODO HAL/BOARD defined section here //_puts("WaveShare RP2350-TOUCH-LCD-2 by 7alken\r\n");
    _puts("Build "); _puts(BUILD); _puts(" / " __DATE__ "-" __TIME__ "\r\n");

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


//TODO HAL/BOARD defined section here
// =========================================================================================
// SPIINIT !! ONLY !! for ESP32-Boards = #define board_esp32
// =========================================================================================

#if !defined(RUNCPM_TARGET_JC8048W535)
    #define SDMHZ 19                    // setting Mhz  - SPI-Bus = SPI_FULL_SPEED = 50Mhz
    #define SDMHZ_TXT "19"              // for outputing SDMHZ-Text
    // normal is 12Mhz because of
    // https://www.pschatzmann.ch/home/2021/03/14/rasperry-pico-with-the-sdfat-library/

    #define SD_CONFIG SdSpiConfig(SD_CS, DEDICATED_SPI, SD_SCK_MHZ(SDMHZ), &SPIBUS)
    // #define SD_CONFIG SdSpiConfig(SD_CS, DEDICATED_SPI, SPI_FULL_SPEED, &SPI)
    // #define SD_CONFIG SdSpiConfig(SD_CS, DEDICATED_SPI, SPI_QUARTER_SPEED, &SPI) //breadboard
    // #define SD_CONFIG SdSpiConfig(SD_CS, DEDICATED_SPI, SPI_HALF_SPEED, &SPI)    //breadboard

// =========================================================================================

    _puts("Init SD SPI bus ......");

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
    _puts("Init SD_MMC pins .....");
    const bool pinsReady = SD.setPins();
    _puts(pinsReady ? "OK\r\n" : "FAILED\r\n");
    _puts("Mount MicroSD card ...");
    const bool storageReady = pinsReady && SD.begin();
#endif

    if (storageReady)
    {
#if defined(RUNCPM_TARGET_JC8048W535)
        _puts("OK (1-bit, CLK=12 CMD=11 D0=13)\r\n");
#if defined(RUNCPM_HEADLESS)
        SD.printRoot(Serial);
#endif
#else
        _puts(SDMHZ_TXT); _puts(" Mhz\r\n");
#endif
#if !defined(RUNCPM_HEADLESS)
        _puts("Terminal profile .....");
        File profileFile = SD.open("A/0/7XTGVTRM.INI", FILE_READ);
        if (profileFile) {
            terminalConfigBegin();
            while (profileFile.available())
                terminalConfigFeed(uint8_t(profileFile.read()));
            profileFile.close();
            unsigned profileErrors = terminalConfigEnd();
            _puts(profileErrors ? "LOADED WITH ERRORS\r\n" : "OK\r\n");
        } else {
            _puts("COMPILED DEFAULTS\r\n");
        }
#endif
        _puts("-------------------------------------------------");


        if (VersionCCP >= 0x10 || SD.exists(CCPname))
        {
            #ifdef ABDOS
              _PatchBIOS();
            #endif

            while (true)
            {
                _puts(CCPHEAD);
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
#elif defined(RUNCPM_TARGET_JC8048W535)
static TaskHandle_t runCPMTaskHandle = nullptr;
static TaskHandle_t runCPMIdleWindowTaskHandle = nullptr;

static void runCPMTask(void *) {
    runCPM();
    vTaskDelete(nullptr);
}

static void runCPMIdleWindowTask(void *) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
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
