# JC8012P4A1C RunCPM port notes

The 10.1-inch JC8012P4A1C is an ESP32-P4 target, while the working terminal and
RunCPM build currently targets RP2350. First validate the panel with the small
standalone Arduino display test.

A later RunCPM port needs these bounded changes:

- replace RP2350 `setup1()` / `loop1()` use with an Arduino FreeRTOS task for
  the CP/M runtime;
- add a display backend for the JD9365 MIPI DSI framebuffer and derive terminal
  geometry from 800x1280 rather than fixed 320x240 coordinates;
- adapt RunCPM storage from the present SPI `SdFat` path to this board's native
  four-bit SDMMC connection;
- keep the CP/M disk layout, terminal parser, palettes, glyph data, and serial
  transport independent of the board backend;
- validate display, SDMMC, serial input, and CP/M separately before combining
  them.

Board facts taken from the supplied resources: ESP32-P4, 16 MB flash, 32 MB
PSRAM, two-lane MIPI DSI JD9365 at 800x1280, LCD reset GPIO 27, backlight GPIO
23, and GSL3680 touch on GPIOs 7/8/21/22.
