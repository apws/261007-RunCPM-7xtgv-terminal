#pragma once

// ============================================================================
// 1. Identity and terminal geometry (no Arduino dependencies)
// ============================================================================
static constexpr char XTGV_BOARD_TARGET_NAME[] = "jc8012p4a1c";
static constexpr int XTGV_TERMINAL_ROWS = 80;
static constexpr int XTGV_TERMINAL_VISIBLE_COLS = 213;
static constexpr vt::DisplayHalDefaults XTGV_DISPLAY_DEFAULTS = {
    "EP4_10", "ESP32-P4 JC8012P4A1C JD9365 10-inch", 1280, 800, 213, XTGV_TERMINAL_ROWS, 2,
    {{212, 80}, {106, 40}, {70, 26}, {52, 20}}, 30, 0, 0, nullptr};

// ============================================================================
// 2. Rendering and backlight policy
// ============================================================================
static constexpr bool XTGV_DISPLAY_NEEDS_FLUSH = true;
// W535-style trial: finish visible dirty cells in one rendering batch instead
// of exposing tiny partial redraws. DSI remains single-buffered, not atomic.
static constexpr bool XTGV_DISPLAY_FULL_FRAME_BUDGET = true;
static constexpr int XTGV_DISPLAY_OFFSET_X = 0;
static constexpr int XTGV_DISPLAY_OFFSET_Y = 0;
static constexpr int XTGV_BACKLIGHT_PIN = 23;

// Petr selected diagnostic level 3 (duty 3) as the minimum lit level.
// Gamma 2.2 shaping: max(level+2, 3+round(252*((level-1)/30)^2.2)).
// Level 0 is OFF; level 31 is full ON.
static constexpr uint8_t XTGV_BACKLIGHT_DUTIES[] = {
    0, 3, 4, 5, 6, 7, 8, 10, 13, 17, 21, 25, 31, 37, 43, 50,
    58, 66, 75, 85, 95, 106, 118, 130, 143, 157, 172, 187, 203,
    220, 237, 255};

// ============================================================================
// 3. Optional compiler policy and storage clock
// ============================================================================

// Requested 40 MHz trial after successful 20 MHz buffer-transfer verification.
// Read configuration at the conservative startup clock.
static constexpr uint32_t XTGV_STORAGE_HZ = 20000000UL;

// Only the sketch defines this guard. Functions below have ordinary external
// linkage; calling them from the terminal does NOT change task/core ownership.
#if defined(XTGV_HAL_IMPLEMENTATION) && !XTGV_DESKTOP
    #include <Arduino.h>
    #if XTGV_TERMINAL
        #include <Arduino_GFX_Library.h>
        #include "../../../src/terminal/line_history.h"
        #include <new>
    #endif

// ============================================================================
// 4. Storage adapter and startup reporting (RunCPM sketch only)
// ============================================================================
#if defined(ARDUINO)
#include <SPI.h>
#include <esp_ldo_regulator.h>
// Target build flags set USE_SPI_ARRAY_TRANSFER=1 for ALL translation units,
// including SdFat sources. Do not define it only here: that would mix driver
// implementations across the sketch/library. Mode 1 uses Arduino's in-place
// buffer API (hardware SPI, not DMA); other boards retain their current policy.
// The current CP/M card is FAT32. Match SdFat32::open() with File32 in the
// RunCPM abstraction; SdFat otherwise selects its FAT/exFAT FsFile alias.
#define SDFAT_FILE_TYPE 1
#include <SdFat.h>
#include "../sdfat_spi_driver.h"

static XtgvSdSpiDriver xtgvStorageSpi(SPI);

// Vendor NewPanel BSP: CLK=43, CMD=44, D0=39, D3=42.
// SPI uses CMD as MOSI, D0 as MISO, D3 as CS. DSI owns no SPI bus.
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
#if !USE_BLOCK_DEVICE_INTERFACE
#error "Read-only storage requires build-wide USE_BLOCK_DEVICE_INTERFACE=1"
#endif

// This test build never hands the physical card to the filesystem. Even a
// write through a File32 or directory handle reaches this rejecting boundary.
class XtgvReadOnlyCard : public FsBlockDeviceInterface {
public:
    bool checkHighSpeed(uint8_t *status) {
        // Fixed mode-0 query, never expose a caller-selected switch argument.
        return card_.cardCMD6(0x00FFFFFFUL, status);
    }
    bool readIdentity(cid_t *cid, csd_t *csd) {
        return card_.readCID(cid) && card_.readCSD(csd);
    }
    uint8_t errorCode() { return card_.errorCode(); }
    bool begin(SdSpiConfig config) { return card_.begin(config); }
    bool isBusy() override { return card_.isBusy(); }
    uint32_t sectorCount() override { return card_.sectorCount(); }
    bool readSector(uint32_t sector, uint8_t *dst) override {
        return card_.readSector(sector, dst);
    }
    bool readSectors(uint32_t sector, uint8_t *dst, size_t count) override {
        return card_.readSectors(sector, dst, count);
    }
    bool syncDevice() override { return card_.syncDevice(); }
    void end() override { card_.end(); }
    bool writeSector(uint32_t, const uint8_t *) override { return false; }
    bool writeSectors(uint32_t, const uint8_t *, size_t) override { return false; }

private:
    SdSpiCard card_;
};

class XtgvReadOnlyStorage : public FatVolume {
public:
    bool checkHighSpeed(uint8_t *status) { return card_.checkHighSpeed(status); }
    bool syncCard() { return card_.syncDevice(); }
    bool readIdentity(cid_t *cid, csd_t *csd) { return card_.readIdentity(cid, csd); }
    uint32_t cardSectors() { return card_.sectorCount(); }
    uint8_t cardError() { return card_.errorCode(); }
    bool readRaw(uint32_t sector, uint8_t *data, size_t count) {
        return card_.readSectors(sector, data, count);
    }
    bool begin(SdSpiConfig config) {
        return card_.begin(config) &&
               (FatVolume::begin(&card_) || FatVolume::begin(&card_, true, 0));
    }
    File32 open(const char *path, oflag_t flags = O_RDONLY) {
        // Whitelist read-only (zero) and no other open flags.
        return flags == O_RDONLY ? FatVolume::open(path, flags) : File32();
    }
    bool remove(const char *) { return false; }
    bool rename(const char *, const char *) { return false; }
    bool mkdir(const char *, bool = true) { return false; }
    bool rmdir(const char *) { return false; }
    bool truncate(const char *, uint32_t) { return false; }

private:
    XtgvReadOnlyCard card_;
};

XtgvReadOnlyStorage SD;
#else
SdFat32 SD;
#endif
// The socket VDD and external pull-ups use ESP_LDO_VO4, not permanent 3V3.
// Retain ownership for the whole mount lifetime; DSI separately uses LDO3.
static esp_ldo_channel_handle_t xtgvStoragePower = nullptr;

// Preserve RunCPM's historical ESP32-only BDOS extension hook.
#define board_esp32
static uint8_t esp32bdos(uint16_t) { return 0; }

XtgvBoardStorageStatus xtgvBoardStorageInit() {
    if (!xtgvStoragePower) {
        esp_ldo_channel_config_t power = {};
        power.chan_id = 4;
        power.voltage_mv = 3300;
        if (esp_ldo_acquire_channel(&power, &xtgvStoragePower) != ESP_OK) {
            return {false, false};
        }
        // Allow card power to settle before driving pins or issuing commands.
        delay(10);
    }
    // Unused SD data inputs must stay high in SPI mode.
    pinMode(40, INPUT_PULLUP);
    pinMode(41, INPUT_PULLUP);
    digitalWrite(42, HIGH);
    pinMode(42, OUTPUT);
    SPI.begin(43, 39, 44, 42); // SCK, MISO, MOSI, CS
    const bool mounted = SD.begin(
        SdSpiConfig(42, DEDICATED_SPI, XTGV_STORAGE_HZ, &xtgvStorageSpi));
    return {true, mounted};
}

// Called only by the card-owner task, after all configuration handles close.
bool xtgvBoardStorageSetMHz(uint8_t mhz) {
    if (mhz < 1 || mhz > 60) return false;
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
    const bool idle = SD.syncCard();
#else
    const bool idle = SD.card()->syncDevice();
#endif
    return idle && xtgvStorageSpi.changeClock(uint32_t(mhz) * 1000000UL);
}

void xtgvBoardPrintStorageStatus(const XtgvBoardStorageStatus &status,
                                void (*write)(const char *)) {
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
    write("*** STORAGE READ-ONLY TEST: all filesystem writes blocked ***\r\n");
#endif
    write("SD power LDO4 3.3V ...");
    write(xtgvStoragePower ? "ON\r\n" : "FAILED\r\n");
    write("Init SD SPI bus ......");
    write(status.pinsReady ? "CONFIGURED\r\n" : "FAILED\r\n");
    write("Mount MicroSD card ...");
    if (status.mediaReady) {
        char detail[80];
        snprintf(detail, sizeof(detail),
                 "OK (SPI, CS=42 MOSI=44 MISO=39 SCK=43, %lu Hz)\r\n",
                 (unsigned long)xtgvStorageSpi.requestedHz());
        write(detail);
    }
}

#endif

// No additional startup credit on this board.
#if defined(ARDUINO)
void xtgvBoardPrintStorageSummary(void (*write)(const char *)) {
    char message[80];
    snprintf(message, sizeof(message),
             "Storage: Arduino SdFat/SPI (%lu MHz)\r\n",
             (unsigned long)(xtgvStorageSpi.requestedHz() / 1000000UL));
    write(message);
}
#endif

void xtgvBoardPrintBootExtra(void (*)(const char *)) {
}

// ============================================================================
// 5. Display creation, initialization and history memory
// ============================================================================
#if XTGV_TERMINAL

// SPDX-License-Identifier: Apache-2.0
// Panel command table adapted from Espressif's JC8012P4A1C NewPanel example.

// JC8012P4A1C NewPanel command sequence from the supplied vendor Arduino
// example. Arduino_GFX defines lcd_init_cmd_t before including this file.
// The first four commands reproduce the generic JD9365 driver's user-page,
// MADCTL, RGB565, and two-lane setup before its vendor-specific sequence.
static const lcd_init_cmd_t xtgvP4NewPanelInit[] = {
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x80, (uint8_t[]){0x01}, 1, 0},
//  {cmd, { data }, data_size, delay_ms}
    // Page selection
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0xE1, (uint8_t[]){0x93}, 1, 0},
    {0xE2, (uint8_t[]){0x65}, 1, 0},
    {0xE3, (uint8_t[]){0xF8}, 1, 0},
    {0x80, (uint8_t[]){0x01}, 1, 0}, // 03->01 2lane

    // User page commands
    {0xE0, (uint8_t[]){0x01}, 1, 0},
    {0x00, (uint8_t[]){0x00}, 1, 0},
    {0x01, (uint8_t[]){0x44}, 1, 0},
    {0x03, (uint8_t[]){0x10}, 1, 0},
    {0x04, (uint8_t[]){0x38}, 1, 0},

    {0x0C, (uint8_t[]){0x74}, 1, 0},

    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x18, (uint8_t[]){0xAF}, 1, 0},
    {0x19, (uint8_t[]){0x00}, 1, 0},
    {0x1A, (uint8_t[]){0x00}, 1, 0},
    {0x1B, (uint8_t[]){0xAF}, 1, 0},
    {0x1C, (uint8_t[]){0x00}, 1, 0},

    {0x35, (uint8_t[]){0x26}, 1, 0},

    {0x37, (uint8_t[]){0x09}, 1, 0},

    {0x38, (uint8_t[]){0x04}, 1, 0},
    {0x39, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x01}, 1, 0},
    {0x3C, (uint8_t[]){0x78}, 1, 0},
    {0x3D, (uint8_t[]){0xFF}, 1, 0},
    {0x3E, (uint8_t[]){0xFF}, 1, 0},
    {0x3F, (uint8_t[]){0x7F}, 1, 0},

    {0x40, (uint8_t[]){0x06}, 1, 0},
    {0x41, (uint8_t[]){0xA0}, 1, 0},
    {0x42, (uint8_t[]){0x81}, 1, 0},
    {0x43, (uint8_t[]){0x1E}, 1, 0},
    {0x44, (uint8_t[]){0x0D}, 1, 0},
    {0x45, (uint8_t[]){0x28}, 1, 0},

    {0x55, (uint8_t[]){0x02}, 1, 0},
    {0x57, (uint8_t[]){0x69}, 1, 0},
    {0x59, (uint8_t[]){0x0A}, 1, 0},
    {0x5A, (uint8_t[]){0x2A}, 1, 0},
    {0x5B, (uint8_t[]){0x17}, 1, 0},

    // G2.2 / G2.5 - Positive Gamma
    {0x5D, (uint8_t[]){0x7F}, 1, 0},
    {0x5E, (uint8_t[]){0x6B}, 1, 0},
    {0x5F, (uint8_t[]){0x5C}, 1, 0},
    {0x60, (uint8_t[]){0x50}, 1, 0},
    {0x61, (uint8_t[]){0x4C}, 1, 0},
    {0x62, (uint8_t[]){0x3E}, 1, 0},
    {0x63, (uint8_t[]){0x41}, 1, 0},
    {0x64, (uint8_t[]){0x2B}, 1, 0},
    {0x65, (uint8_t[]){0x43}, 1, 0},
    {0x66, (uint8_t[]){0x42}, 1, 0},
    {0x67, (uint8_t[]){0x43}, 1, 0},
    {0x68, (uint8_t[]){0x62}, 1, 0},
    {0x69, (uint8_t[]){0x52}, 1, 0},
    {0x6A, (uint8_t[]){0x5A}, 1, 0},
    {0x6B, (uint8_t[]){0x4C}, 1, 0},
    {0x6C, (uint8_t[]){0x48}, 1, 0},
    {0x6D, (uint8_t[]){0x3A}, 1, 0},
    {0x6E, (uint8_t[]){0x28}, 1, 0},
    {0x6F, (uint8_t[]){0x10}, 1, 0},

    // Negative Gamma
    {0x70, (uint8_t[]){0x7F}, 1, 0},
    {0x71, (uint8_t[]){0x6B}, 1, 0},
    {0x72, (uint8_t[]){0x5C}, 1, 0},
    {0x73, (uint8_t[]){0x50}, 1, 0},
    {0x74, (uint8_t[]){0x4C}, 1, 0},
    {0x75, (uint8_t[]){0x3E}, 1, 0},
    {0x76, (uint8_t[]){0x41}, 1, 0},
    {0x77, (uint8_t[]){0x2B}, 1, 0},
    {0x78, (uint8_t[]){0x43}, 1, 0},
    {0x79, (uint8_t[]){0x42}, 1, 0},
    {0x7A, (uint8_t[]){0x43}, 1, 0},
    {0x7B, (uint8_t[]){0x62}, 1, 0},
    {0x7C, (uint8_t[]){0x52}, 1, 0},
    {0x7D, (uint8_t[]){0x5A}, 1, 0},
    {0x7E, (uint8_t[]){0x4C}, 1, 0},
    {0x7F, (uint8_t[]){0x48}, 1, 0},
    {0x80, (uint8_t[]){0x3A}, 1, 0},
    {0x81, (uint8_t[]){0x28}, 1, 0},
    {0x82, (uint8_t[]){0x10}, 1, 0},

    // Page 2
    {0xE0, (uint8_t[]){0x02}, 1, 0},
    {0x00, (uint8_t[]){0x42}, 1, 0},
    {0x01, (uint8_t[]){0x42}, 1, 0},
    {0x02, (uint8_t[]){0x40}, 1, 0},
    {0x03, (uint8_t[]){0x40}, 1, 0},
    {0x04, (uint8_t[]){0x5E}, 1, 0},
    {0x05, (uint8_t[]){0x5E}, 1, 0},
    {0x06, (uint8_t[]){0x5F}, 1, 0},
    {0x07, (uint8_t[]){0x5F}, 1, 0},
    {0x08, (uint8_t[]){0x5F}, 1, 0},
    {0x09, (uint8_t[]){0x57}, 1, 0},
    {0x0A, (uint8_t[]){0x57}, 1, 0},
    {0x0B, (uint8_t[]){0x77}, 1, 0},
    {0x0C, (uint8_t[]){0x77}, 1, 0},
    {0x0D, (uint8_t[]){0x47}, 1, 0},
    {0x0E, (uint8_t[]){0x47}, 1, 0},
    {0x0F, (uint8_t[]){0x45}, 1, 0},
    {0x10, (uint8_t[]){0x45}, 1, 0},
    {0x11, (uint8_t[]){0x4B}, 1, 0},
    {0x12, (uint8_t[]){0x4B}, 1, 0},
    {0x13, (uint8_t[]){0x49}, 1, 0},
    {0x14, (uint8_t[]){0x49}, 1, 0},
    {0x15, (uint8_t[]){0x5F}, 1, 0},

    {0x16, (uint8_t[]){0x41}, 1, 0},
    {0x17, (uint8_t[]){0x41}, 1, 0},
    {0x18, (uint8_t[]){0x40}, 1, 0},
    {0x19, (uint8_t[]){0x40}, 1, 0},
    {0x1A, (uint8_t[]){0x5E}, 1, 0},
    {0x1B, (uint8_t[]){0x5E}, 1, 0},
    {0x1C, (uint8_t[]){0x5F}, 1, 0},
    {0x1D, (uint8_t[]){0x5F}, 1, 0},
    {0x1E, (uint8_t[]){0x5F}, 1, 0},
    {0x1F, (uint8_t[]){0x57}, 1, 0},
    {0x20, (uint8_t[]){0x57}, 1, 0},
    {0x21, (uint8_t[]){0x77}, 1, 0},
    {0x22, (uint8_t[]){0x77}, 1, 0},
    {0x23, (uint8_t[]){0x46}, 1, 0},
    {0x24, (uint8_t[]){0x46}, 1, 0},
    {0x25, (uint8_t[]){0x44}, 1, 0},
    {0x26, (uint8_t[]){0x44}, 1, 0},
    {0x27, (uint8_t[]){0x4A}, 1, 0},
    {0x28, (uint8_t[]){0x4A}, 1, 0},
    {0x29, (uint8_t[]){0x48}, 1, 0},
    {0x2A, (uint8_t[]){0x48}, 1, 0},
    {0x2B, (uint8_t[]){0x5F}, 1, 0},

    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x2D, (uint8_t[]){0x01}, 1, 0},
    {0x2E, (uint8_t[]){0x00}, 1, 0},
    {0x2F, (uint8_t[]){0x00}, 1, 0},
    {0x30, (uint8_t[]){0x1F}, 1, 0},
    {0x31, (uint8_t[]){0x1F}, 1, 0},
    {0x32, (uint8_t[]){0x1E}, 1, 0},
    {0x33, (uint8_t[]){0x1E}, 1, 0},
    {0x34, (uint8_t[]){0x1F}, 1, 0},
    {0x35, (uint8_t[]){0x17}, 1, 0},
    {0x36, (uint8_t[]){0x17}, 1, 0},
    {0x37, (uint8_t[]){0x37}, 1, 0},
    {0x38, (uint8_t[]){0x37}, 1, 0},
    {0x39, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x08}, 1, 0},
    {0x3B, (uint8_t[]){0x0A}, 1, 0},
    {0x3C, (uint8_t[]){0x0A}, 1, 0},
    {0x3D, (uint8_t[]){0x04}, 1, 0},
    {0x3E, (uint8_t[]){0x04}, 1, 0},
    {0x3F, (uint8_t[]){0x06}, 1, 0},
    {0x40, (uint8_t[]){0x06}, 1, 0},
    {0x41, (uint8_t[]){0x1F}, 1, 0},

    {0x42, (uint8_t[]){0x02}, 1, 0},
    {0x43, (uint8_t[]){0x02}, 1, 0},
    {0x44, (uint8_t[]){0x00}, 1, 0},
    {0x45, (uint8_t[]){0x00}, 1, 0},
    {0x46, (uint8_t[]){0x1F}, 1, 0},
    {0x47, (uint8_t[]){0x1F}, 1, 0},
    {0x48, (uint8_t[]){0x1E}, 1, 0},
    {0x49, (uint8_t[]){0x1E}, 1, 0},
    {0x4A, (uint8_t[]){0x1F}, 1, 0},
    {0x4B, (uint8_t[]){0x17}, 1, 0},
    {0x4C, (uint8_t[]){0x17}, 1, 0},
    {0x4D, (uint8_t[]){0x37}, 1, 0},
    {0x4E, (uint8_t[]){0x37}, 1, 0},
    {0x4F, (uint8_t[]){0x09}, 1, 0},
    {0x50, (uint8_t[]){0x09}, 1, 0},
    {0x51, (uint8_t[]){0x0B}, 1, 0},
    {0x52, (uint8_t[]){0x0B}, 1, 0},
    {0x53, (uint8_t[]){0x05}, 1, 0},
    {0x54, (uint8_t[]){0x05}, 1, 0},
    {0x55, (uint8_t[]){0x07}, 1, 0},
    {0x56, (uint8_t[]){0x07}, 1, 0},
    {0x57, (uint8_t[]){0x1F}, 1, 0},

    {0x58, (uint8_t[]){0x40}, 1, 0},
    {0x5B, (uint8_t[]){0x30}, 1, 0},
    {0x5C, (uint8_t[]){0x00}, 1, 0},
    {0x5D, (uint8_t[]){0x34}, 1, 0},
    {0x5E, (uint8_t[]){0x05}, 1, 0},
    {0x5F, (uint8_t[]){0x02}, 1, 0},
    {0x63, (uint8_t[]){0x00}, 1, 0},
    {0x64, (uint8_t[]){0x6A}, 1, 0},
    {0x67, (uint8_t[]){0x73}, 1, 0},
    {0x68, (uint8_t[]){0x07}, 1, 0},
    {0x69, (uint8_t[]){0x08}, 1, 0},
    {0x6A, (uint8_t[]){0x6A}, 1, 0},
    {0x6B, (uint8_t[]){0x08}, 1, 0},

    {0x6C, (uint8_t[]){0x00}, 1, 0},
    {0x6D, (uint8_t[]){0x00}, 1, 0},
    {0x6E, (uint8_t[]){0x00}, 1, 0},
    {0x6F, (uint8_t[]){0x88}, 1, 0},

    {0x75, (uint8_t[]){0xFF}, 1, 0},
    {0x77, (uint8_t[]){0xDD}, 1, 0},
    {0x78, (uint8_t[]){0x2C}, 1, 0},
    {0x79, (uint8_t[]){0x15}, 1, 0},
    {0x7A, (uint8_t[]){0x17}, 1, 0},
    {0x7D, (uint8_t[]){0x14}, 1, 0},
    {0x7E, (uint8_t[]){0x82}, 1, 0},

    // Page 4
    {0xE0, (uint8_t[]){0x04}, 1, 0},
    {0x00, (uint8_t[]){0x0E}, 1, 0},
    {0x02, (uint8_t[]){0xB3}, 1, 0},
    {0x09, (uint8_t[]){0x60}, 1, 0},
    {0x0E, (uint8_t[]){0x48}, 1, 0},
    {0x37, (uint8_t[]){0x58}, 1, 0},
    {0x2B, (uint8_t[]){0x0F}, 1, 0},

    // Page 5
    {0xE0, (uint8_t[]){0x05}, 1, 0},
    {0x15, (uint8_t[]){0x1D}, 1, 0},

    // Return to user page
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0xE6, (uint8_t[]){0x02}, 1, 0},
    {0xE7, (uint8_t[]){0x0C}, 1, 0},

    // Sleep out
    {0x11, (uint8_t[]){0x00}, 1, 0},
    {0x00, (uint8_t[]){0x00}, 1, 120},

    // Display on
    {0x29, (uint8_t[]){0x00}, 1, 0},
    {0x00, (uint8_t[]){0x00}, 1, 20},
};

Arduino_GFX *xtgvBoardCreateDisplay() {
    // Preserve NewPanel blanking and rotation; trial 40 MHz pixel clock to
    // reduce PSRAM scanout demand after reported DSI underruns (~34.3 Hz).
    auto *panel = new Arduino_ESP32DSIPanel(
        20, 20, 40, 4, 10, 30, 40000000, 1500);
    return new Arduino_DSI_Display(
        800, 1280, panel, 1, false, 27,
        xtgvP4NewPanelInit, sizeof(xtgvP4NewPanelInit) / sizeof(lcd_init_cmd_t));
}

bool xtgvBoardBeginDisplay(Arduino_GFX *display) {
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR < 3
        display->begin();
        return true;
    #else
        return display->begin();
    #endif
}

vt::LineHistory *xtgvBoardCreateHistory() {
    // Ten history banks live in PSRAM, leaving internal RAM for rendering.
    void *memory = ps_malloc(sizeof(vt::LineHistory));
    return memory ? new (memory) vt::LineHistory : nullptr;
}

// ============================================================================
// 6. Physical backlight output (logical levels/fade remain terminal behavior)
// ============================================================================
void xtgvBoardBacklightInit() {
    // Set inactive latch before enabling the active-high output.
    digitalWrite(XTGV_BACKLIGHT_PIN, LOW);
    pinMode(XTGV_BACKLIGHT_PIN, OUTPUT);
}

void xtgvBoardBacklightWrite(uint8_t duty) {
    analogWrite(XTGV_BACKLIGHT_PIN, duty);
}
#endif

// ============================================================================
// 7. Whole-board reboot
// ============================================================================
void runCPMHardwareReboot() {
    #if defined(ARDUINO)
        #if XTGV_TERMINAL
            // Blank before restarting, not only when terminalSetup runs again.
            // Stop PWM ownership and drive LOW (this backlight is active-high).
            // This does not guarantee its state throughout ROM/bootloader reset.
            ledcWrite(XTGV_BACKLIGHT_PIN, 0);
            ledcDetach(XTGV_BACKLIGHT_PIN);
            pinMode(XTGV_BACKLIGHT_PIN, OUTPUT);
            digitalWrite(XTGV_BACKLIGHT_PIN, LOW);
        #endif
        ESP.restart();
    #endif
}

#endif // XTGV_HAL_IMPLEMENTATION
