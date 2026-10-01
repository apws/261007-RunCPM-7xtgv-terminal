#pragma once
// Waveshare RP2350-LCD-1.47-A schematic, SD_CARD and GPIO net table:
// https://files.waveshare.com/wiki/RP2350-LCD-1.47/RP2350-LCD-1.47-A.pdf
// Dedicated SPI1 belongs to CP/M core 1; TFT uses SPI0 on separate pins.
#define SPIBUS SPI1
#ifndef SD_CS
#define SD_CS 15
#endif
#define SD_SCK 10
#define SD_MOSI 11
#define SD_MISO 12
#define RUNCPM_SD_MHZ 12
#define RUNCPM_SD_MHZ_TEXT "12"
#define XTGV_BOARD_BOOT_EXTRA ""
#define XTGV_BOARD_STORAGE_INIT_LABEL "Init SD SPI bus ......"
// No activity LED: GPIO21 belongs to the backlight, GPIO22 to a WS2812.
