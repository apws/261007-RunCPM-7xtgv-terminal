"""Run the installed pinned SdFat buffer driver with a mock Arduino SPI API."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class SdFatArrayTests(unittest.TestCase):
    def test_read_and_write_buffers(self):
        libraries = sorted((Path.home() / '.arduino15/internal').glob(
            'SdFat_2.3.0_*/SdFat/src/SpiDriver'))
        if not libraries:
            self.skipTest('Pinned SdFat 2.3.0 is not installed')
        source = r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <vector>
#define SPI_DRIVER_SELECT 1
#define USE_SPI_ARRAY_TRANSFER 1
constexpr int MSBFIRST = 1, SPI_MODE0 = 0, USER_SPI_BEGIN = 1;
struct SPISettings { SPISettings() {} SPISettings(uint32_t, int, int) {} };
static bool reading = false;
static int byteCalls = 0, bufferCalls = 0;
static std::vector<uint8_t> sent;
class SPIClass {
public:
    void begin() {}
    void end() {}
    void beginTransaction(SPISettings) {}
    void endTransaction() {}
    uint8_t transfer(uint8_t) { ++byteCalls; return 0; }
    void transfer(void *data, size_t n) {
        ++bufferCalls;
        auto *p = static_cast<uint8_t *>(data);
        for (size_t i = 0; i < n; ++i) {
            if (reading) assert(p[i] == 0xff);
            else sent.push_back(p[i]);
            p[i] = 0x5a;
        }
    }
} SPI;
struct SdSpiConfig { SPIClass *spiPort; int options; };
#include "SdSpiArduinoDriver.h"
int main() {
    SdSpiArduinoDriver driver;
    driver.begin({&SPI, USER_SPI_BEGIN});
    driver.activate();
    uint8_t block[513];
    reading = true;
    assert(driver.receive(block, 512) == 0);
    assert(bufferCalls == 1 && byteCalls == 0);
    for (int i = 0; i < 512; ++i) assert(block[i] == 0x5a);
    reading = false;
    for (int i = 0; i < 513; ++i) block[i] = uint8_t(i);
    driver.send(block, 513); // Exercise the 512-byte write scratch buffer tail.
    assert(bufferCalls == 3 && byteCalls == 0 && sent.size() == 513);
    for (int i = 0; i < 513; ++i) {
        assert(block[i] == uint8_t(i)); // Caller buffer must not be overwritten.
        assert(sent[i] == uint8_t(i));
    }
    driver.deactivate();
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-sdfat-array-') as tmp:
            binary = Path(tmp) / 'test'
            result = subprocess.run([
                'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I', str(libraries[0]), '-x', 'c++', '-', '-o', str(binary),
            ], input=source, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=5)
