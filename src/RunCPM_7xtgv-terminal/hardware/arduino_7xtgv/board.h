#pragma once

// One board selection for the current integrated RunCPM/terminal sketch.
#if defined(RUNCPM_TARGET_JC8048W535)
#include "boards/jc8048w535_sdmmc.h"
#elif defined(RUNCPM_TARGET_JC8048W550)
#include "boards/jc8048w550_sd_spi.h"
#elif defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
#include "boards/pico_rp2350_lcd_1_47_a_sd_spi.h"
#elif defined(RUNCPM_TARGET_RP2350_TOUCH_LCD_2)
#include "boards/pico_rp2350_touch_lcd_2_sd_spi.h"
#else
#error "No RunCPM board target selected"
#endif

#if defined(RUNCPM_TARGET_JC8048W535) || defined(RUNCPM_TARGET_JC8048W550)
RunCPMStorage SD;
#else
#include <SPI.h>
#include <SdFat.h>

SdFat32 SD; // 32-bit file sizes, matching RunCPM's File32 usage
typedef File32 File;
#endif

struct XtgvBoardStorageStatus {
    bool pinsReady;
    bool mediaReady;
};

inline XtgvBoardStorageStatus xtgvBoardStorageInit() {
#if defined(RUNCPM_TARGET_JC8048W535) || defined(RUNCPM_TARGET_JC8048W550)
    const bool pinsReady = SD.setPins();
    return {pinsReady, pinsReady && SD.begin()};
#else
    // RP2350: dedicated SPI1 belongs to the SD owner; the TFT uses another bus.
    // See https://www.pschatzmann.ch/home/2021/03/14/rasperry-pico-with-the-sdfat-library/
    SPIBUS.setSCK(SD_SCK);
    SPIBUS.setTX(SD_MOSI);
    SPIBUS.setRX(SD_MISO);
    return {true, SD.begin(SdSpiConfig(SD_CS, DEDICATED_SPI,
                                       SD_SCK_MHZ(RUNCPM_SD_MHZ), &SPIBUS))};
#endif
}

// A board may contribute a startup credit without changing the common RunCPM
// banner. Current boards leave this explicit policy slot empty.
inline void xtgvBoardPrintBootExtra(void (*write)(const char *)) {
    if (XTGV_BOARD_BOOT_EXTRA[0]) {
        write(XTGV_BOARD_BOOT_EXTRA);
        write("\r\n");
    }
}

inline void xtgvBoardPrintStorageStatus(const XtgvBoardStorageStatus &status,
                                        void (*write)(const char *)) {
    write(XTGV_BOARD_STORAGE_INIT_LABEL);
#if defined(RUNCPM_TARGET_JC8048W535) || defined(RUNCPM_TARGET_JC8048W550)
    write(status.pinsReady ? "OK\r\n" : "FAILED\r\n");
#else
    write("CONFIGURED\r\n");
#endif
    write("Mount MicroSD card ...");
    if (!status.mediaReady)
        return;
#if defined(RUNCPM_TARGET_JC8048W535)
    write("OK (1-bit, CLK=12 CMD=11 D0=13)\r\n");
#if !XTGV_TERMINAL
    SD.printRoot(Serial);
#endif
#elif defined(RUNCPM_TARGET_JC8048W550)
    char detail[80];
    snprintf(detail, sizeof(detail),
             "OK (SPI, CS=10 MOSI=11 MISO=13 SCK=12, %lu Hz)\r\n",
             (unsigned long)RUNCPM_W550_SD_EFFECTIVE_HZ);
    write(detail);
#else
    write(RUNCPM_SD_MHZ_TEXT);
    write(" Mhz\r\n");
#endif
}
