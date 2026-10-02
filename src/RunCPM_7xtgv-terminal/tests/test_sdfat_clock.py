"""Native tests of the runtime-clock transport and global INI parser."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ClockTests(unittest.TestCase):
    def compile_run(self, source, extras=()):
        with tempfile.TemporaryDirectory(prefix='xtgv-sd-clock-') as directory:
            binary = Path(directory) / 'test'
            result = subprocess.run([
                'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT), '-x', 'c++', '-', *map(str, extras),
                '-o', str(binary)], input=source, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_three_transfer_policies_and_transaction_clock(self):
        header = (ROOT / 'hardware/arduino_7xtgv/sdfat_spi_driver.h').read_text()
        implementation = header[header.index('class XtgvSdSpiDriver'):]
        stub = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <vector>
constexpr int MSBFIRST = 1, SPI_MODE0 = 0;
struct SdSpiConfig {};
struct SPISettings { uint32_t hz; SPISettings(uint32_t h,int,int):hz(h){} };
struct SdSpiBaseClass {
    virtual ~SdSpiBaseClass() = default;
    virtual void begin(SdSpiConfig)=0;
    virtual void end()=0;
    virtual void activate()=0;
    virtual void deactivate()=0;
    virtual void setSckSpeed(uint32_t)=0;
    virtual uint8_t receive()=0;
    virtual uint8_t receive(uint8_t*,size_t)=0;
    virtual void send(uint8_t)=0;
    virtual void send(const uint8_t*,size_t)=0;
};
struct SPIClass {
    uint32_t clock=0; int starts=0,stops=0,bytes=0,buffers=0;
    std::vector<uint8_t> transmitted;
    void begin() {}
    void beginTransaction(SPISettings s) { clock=s.hz; ++starts; }
    void endTransaction() { ++stops; }
    uint8_t transfer(uint8_t x) { ++bytes; transmitted.push_back(x); return 0x5a; }
    void transfer(uint8_t *p,size_t n) {
        ++buffers;
        transmitted.insert(transmitted.end(),p,p+n);
        memset(p,0x5a,n);
    }
    void transfer(void *tx,void *rx,size_t n) {
        ++buffers;
        if(tx) { auto *p=static_cast<uint8_t*>(tx); transmitted.insert(transmitted.end(),p,p+n); }
        if(rx) memset(rx,0x5a,n);
    }
};
'''
        main = r'''
int main() {
    SPIClass spi;
    XtgvSdSpiDriver driver(spi);
    driver.begin({});
    driver.setSckSpeed(400000); // SdFat initialization speed remains supported.
    driver.activate();
    assert(spi.clock==400000 && !driver.changeClock(50000000));
    driver.deactivate();
    assert(!driver.changeClock(0) && !driver.changeClock(61000000));
    assert(driver.changeClock(50000000));
    driver.activate();
    assert(spi.clock==50000000);
    uint8_t receive[513];
    driver.receive(receive,sizeof(receive));
    for(auto byte:receive) assert(byte==0x5a);
    uint8_t send[513]; memset(send,0xa5,sizeof(send));
    spi.transmitted.clear();
    driver.send(send,sizeof(send));
    for(auto byte:send) assert(byte==0xa5);
    assert(spi.transmitted.size()==513);
    for(auto byte:spi.transmitted) assert(byte==0xa5);
#if USE_SPI_ARRAY_TRANSFER == 0
    assert(spi.buffers==0 && spi.bytes==1026);
#else
    assert(spi.bytes==0 && spi.buffers>=2);
#endif
    driver.deactivate();
    assert(driver.changeClock(20000000));
    driver.activate(); assert(spi.clock==20000000);
    driver.end(); assert(spi.starts==spi.stops);
}
'''
        for policy in (0, 1, 2):
            with self.subTest(policy=policy):
                self.compile_run(f'#define USE_SPI_ARRAY_TRANSFER {policy}\n' +
                                 stub + implementation + main)

    def test_ini_clock(self):
        source = r'''
#include <cassert>
#include <cstring>
#include "src/terminal/history_profiles.h"
static void load(vt::HistoryProfiles &p, const char *text) {
    p.beginLoad(); while (*text) p.feed(uint8_t(*text++)); p.finishLoad();
}
int main() {
    vt::HistoryProfiles p;
    load(p,"[global]\n"); assert(p.global().sdfatSpiMHz==20);
    load(p,"[global]\nsdfat_spi=50\n"); assert(p.global().sdfatSpiMHz==50);
    load(p,"[global]\nsdfat_spi=1\n"); assert(p.global().sdfatSpiMHz==1);
    load(p,"[global]\nsdfat_spi=60\n"); assert(p.global().sdfatSpiMHz==60);
    for (const char *s : {"0", "61", "-1", "50MHz", "", "no"}) {
        char text[80]; snprintf(text,sizeof(text),"[global]\nsdfat_spi=%s\n",s);
        load(p,text); assert(p.global().sdfatSpiMHz==20);
    }
    load(p,"[global]\nsdfat_spi=40\nsdfat_spi=bad\n");
    assert(p.global().sdfatSpiMHz==20);
    load(p,"[global]\n"); assert(p.global().sdfatSpiMHz==20);
}
'''
        self.compile_run('#include <cstdio>\n#include <initializer_list>\n' + source,
                         [ROOT / 'src/terminal/history_profiles.cpp'])
