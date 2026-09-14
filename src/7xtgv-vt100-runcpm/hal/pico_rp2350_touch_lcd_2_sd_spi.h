#pragma once
// Reconstructed from the Waveshare RP2350-Touch-LCD-2 schematic.
// https://files.waveshare.com/wiki/RP2350-Touch-LCD-2/RP2350-Touch-LCD-2.pdf
#define SPIBUS SPI1
#ifndef SD_CS
#define SD_CS 25
#endif
#define SD_SCK 26
#define SD_MOSI 27
#define SD_MISO 24
// Reliability takes priority over benchmark throughput. This remains a
// dedicated SPI1 bus; the TFT uses its own PIO-backed bus and pins.
#define RUNCPM_SD_MHZ 12
#define RUNCPM_SD_MHZ_TEXT "12"
// GPIO15 is the TFT backlight, not a dedicated activity LED.
// Leave LED undefined so disk activity cannot override display brightness.
