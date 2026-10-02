"""Exercise P4's actual storage initializer with power/SPI test doubles."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class P4StorageTests(unittest.TestCase):
    def test_read_only_sector_boundary_and_file_rejections(self):
        board = (ROOT / 'hardware/arduino_7xtgv/boards/jc8012p4a1c.h').read_text()
        classes = board[board.index('class XtgvReadOnlyCard'):board.index('XtgvReadOnlyStorage SD;')]
        source = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
using oflag_t = unsigned;
constexpr oflag_t O_RDONLY = 0;
struct SdSpiConfig {};
struct cid_t {};
struct csd_t {};
struct FsBlockDeviceInterface {
    virtual ~FsBlockDeviceInterface() = default;
    virtual void end() {}
    virtual bool isBusy() = 0;
    virtual uint32_t sectorCount() = 0;
    virtual bool syncDevice() = 0;
    virtual bool readSector(uint32_t, uint8_t *) = 0;
    virtual bool readSectors(uint32_t, uint8_t *, size_t) = 0;
    virtual bool writeSector(uint32_t, const uint8_t *) = 0;
    virtual bool writeSectors(uint32_t, const uint8_t *, size_t) = 0;
};
static int reads = 0, writes = 0, opens = 0;
static bool cardOK = true;
struct SdSpiCard {
    bool readCID(cid_t *) { return true; }
    bool readCSD(csd_t *) { return true; }
    uint8_t errorCode() { return 0; }
    bool begin(SdSpiConfig) { return cardOK; }
    void end() {}
    bool isBusy() { return false; }
    uint32_t sectorCount() { return 1000; }
    bool syncDevice() { return true; }
    bool readSector(uint32_t, uint8_t *) { ++reads; return true; }
    bool readSectors(uint32_t, uint8_t *, size_t) { ++reads; return true; }
    bool writeSector(uint32_t, const uint8_t *) { ++writes; return true; }
    bool writeSectors(uint32_t, const uint8_t *, size_t) { ++writes; return true; }
};
struct File32 { bool valid = false; };
struct FatVolume {
    FsBlockDeviceInterface *device = nullptr;
    bool begin(FsBlockDeviceInterface *d, bool = true, uint8_t = 1) {
        device = d;
        uint8_t data[512] = {};
        return d->readSector(0, data);
    }
    File32 open(const char *, oflag_t) { ++opens; return {true}; }
};
''' + classes + r'''
int main() {
    XtgvReadOnlyStorage storage;
    cardOK = false;
    assert(!storage.begin({}) && !storage.device);
    cardOK = true;
    assert(storage.begin({}));
    uint8_t data[1024] = {};
    auto *device = storage.device;
    assert(device->readSector(1, data));
    assert(device->readSectors(2, data, 2));
    assert(reads == 3);
    assert(!device->writeSector(1, data));
    assert(!device->writeSectors(2, data, 2));
    assert(writes == 0);
    assert(device->sectorCount() == 1000 && device->syncDevice());
    assert(storage.open("FILE").valid && opens == 1);
    for (unsigned flags = 1; flags < 256; ++flags)
        assert(!storage.open("FILE", flags).valid);
    assert(opens == 1);
    assert(!storage.remove("FILE") && !storage.rename("A", "B"));
    assert(!storage.mkdir("DIR") && !storage.rmdir("DIR"));
    assert(!storage.truncate("FILE", 0));
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-p4-readonly-') as temp:
            binary = Path(temp) / 'test'
            result = subprocess.run(
                ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                 '-x', 'c++', '-', '-o', str(binary)],
                input=source, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_power_precedes_spi_and_is_retained(self):
        board = (ROOT / 'hardware/arduino_7xtgv/boards/jc8012p4a1c.h').read_text()
        start = board.index('XtgvBoardStorageStatus xtgvBoardStorageInit()')
        end = board.index('// Called only by the card-owner task', start)
        source = r'''
#include <cassert>
struct XtgvBoardStorageStatus { bool pinsReady, mediaReady; };
struct esp_ldo_channel_config_t { int chan_id, voltage_mv; };
using esp_ldo_channel_handle_t = void *;
static esp_ldo_channel_handle_t xtgvStoragePower = nullptr;
constexpr int ESP_OK = 0, INPUT_PULLUP = 2, HIGH = 1, OUTPUT = 1;
constexpr int DEDICATED_SPI = 1;
''' + board[board.index('static constexpr uint32_t XTGV_STORAGE_HZ'):board.index(';', board.index('static constexpr uint32_t XTGV_STORAGE_HZ')) + 1].replace('uint32_t', 'unsigned long') + r'''
static int acquisitions = 0, waits = 0, spiStarts = 0, mounts = 0;
static bool failPower = true, mountOK = false;
int esp_ldo_acquire_channel(const esp_ldo_channel_config_t *cfg, void **handle) {
    ++acquisitions;
    assert(cfg->chan_id == 4 && cfg->voltage_mv == 3300);
    if (failPower) return -1;
    *handle = &acquisitions;
    return ESP_OK;
}
void delay(int ms) { assert(xtgvStoragePower && ms == 10); ++waits; }
void pinMode(int, int) { assert(xtgvStoragePower && waits == 1); }
void digitalWrite(int, int) { assert(xtgvStoragePower && waits == 1); }
struct Spi {
    void begin(int clk, int miso, int mosi, int cs) {
        assert(xtgvStoragePower && waits == 1);
        assert(clk == 43 && miso == 39 && mosi == 44 && cs == 42);
        ++spiStarts;
    }
} SPI;
Spi &xtgvStorageSpi = SPI;
struct SdSpiConfig {
    SdSpiConfig(int cs, int mode, int hz, Spi *spi) {
        assert(cs == 42 && mode == DEDICATED_SPI && hz == 20000000 && spi == &SPI);
    }
};
struct Card {
    bool begin(SdSpiConfig) { assert(spiStarts > mounts); ++mounts; return mountOK; }
} SD;
''' + board[start:end] + r'''
int main() {
    auto state = xtgvBoardStorageInit();
    assert(!state.pinsReady && !state.mediaReady);
    assert(acquisitions == 1 && !waits && !spiStarts && !mounts);
    failPower = false;
    state = xtgvBoardStorageInit();
    assert(state.pinsReady && !state.mediaReady);
    assert(acquisitions == 2 && waits == 1 && mounts == 1);
    mountOK = true;
    state = xtgvBoardStorageInit();
    assert(state.pinsReady && state.mediaReady);
    assert(acquisitions == 2 && waits == 1 && mounts == 2);
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-p4-power-') as temp:
            binary = Path(temp) / 'test'
            result = subprocess.run(
                ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                 '-x', 'c++', '-', '-o', str(binary)],
                input=source, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=5)
