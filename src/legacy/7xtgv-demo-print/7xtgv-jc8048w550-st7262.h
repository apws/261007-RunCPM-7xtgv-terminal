#pragma once

// Minimal ST7262 command sequence from the supplied JC8048W550 board file.
// Arduino_ESP32SPI with DC undefined emits the required 9-bit SPI framing.
static const uint8_t jc8048w550_st7262_init[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
    WRITE_COMMAND_8, 0xC0,
    WRITE_BYTES, 2, 0x3B, 0x00,
    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 2, 0x0D, 0x02,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,
    BEGIN_WRITE,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x66,
    WRITE_COMMAND_8, 0x29,
    END_WRITE,
};
