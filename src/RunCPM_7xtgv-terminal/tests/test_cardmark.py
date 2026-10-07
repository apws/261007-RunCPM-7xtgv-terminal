"""Exercise actual native CARDMARK backend with deterministic card test doubles."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CardmarkTests(unittest.TestCase):
    def test_arguments_compare_cancel_and_restore(self):
        source = r'''
#include <cstdint>
#include <string>
#include <cassert>
#include <cstring>
#define RUNCPM_TARGET_JC8012P4A1C 1
#define SPI_DRIVER_SELECT 3
#define XTGV_STORAGE_READ_ONLY 1
static unsigned mhz=40, reads=0;
static bool corrupt=false, readFail=false, abortTest=false, restoreFail=false;
static bool cmd6OK=true, highSpeed=true;
static unsigned cmd6Calls=0;
static uint8_t csdBytes[16]={0x40,0x0E,0,0x32,0x5B,0x59,0,0,0x77,0xC7,0x7F,0x80,0x0A,0x40,0,1};
static std::string output;
struct cid_t {
    uint8_t mid=1; char oid[2]={'A','B'}, pnm[5]={'T','E','S','T','1'};
    unsigned prvN(){return 1;} unsigned prvM(){return 2;}
    uint32_t psn(){return 123;} unsigned mdtYear(){return 2026;}
    unsigned mdtMonth(){return 10;}
    uint8_t padding[8]={};
};
struct csd_t { uint8_t data[16]={}; };
struct Driver { uint32_t requestedHz(){return mhz*1000000;} } xtgvStorageSpi;
bool xtgvBoardStorageSetMHz(uint8_t value) {
    if (restoreFail && value==40) return false;
    mhz=value; return true;
}
struct Card {
    Card *card(){return this;}
    bool readCID(cid_t*){return true;}
    bool readCSD(csd_t *csd){memcpy(csd,csdBytes,16);return true;}
    bool cardCMD6(uint32_t arg,uint8_t *status){
        assert(arg==0x00FFFFFF && mhz==20);
        ++cmd6Calls; status[13]=highSpeed ? 2 : 0; return cmd6OK;
    }
    bool checkHighSpeed(uint8_t *status){return cardCMD6(0x00FFFFFF,status);}
    uint32_t sectorCount(){return cardSectors();}
    uint8_t errorCode(){return cardError();}
    bool readSectors(uint32_t sector,uint8_t *p,size_t n){return readRaw(sector,p,n);}
    bool readIdentity(cid_t*,csd_t *csd){return readCSD(csd);}
    uint32_t cardSectors(){return 31399936;}
    uint8_t cardError(){return 9;}
    bool readRaw(uint32_t sector,uint8_t *p,size_t n) {
        assert(output.find("\r\n ... ")!=std::string::npos);
        assert(output.find("PASS:")==std::string::npos);
        ++reads;
        if(readFail && mhz==50) return false;
        for(size_t i=0;i<n*512;++i) p[i]=uint8_t(sector+i);
        if(corrupt && mhz==50) p[17]^=1;
        return true;
    }
} SD;
uint32_t micros(){static uint32_t t=0; return t+=100;}
void delay(unsigned ms){if(ms==100) throw 1;}
void print(const char *s){output+=s;}
bool cancel(){return abortTest;}
#include "hardware/arduino_7xtgv/cardmark.h"
int main() {
    uint32_t args[3]={1024,40,0};
    bool verbose=false;
    assert(xtgvCardmarkArguments(" 9 csd 50 20 ",args,verbose) && verbose);
    assert(args[0]==9 && args[1]==50 && args[2]==20);
    for(auto text:{"0","1 0","1 61","-1","4294967296","1 50 0 1","1 50x","CSD CSD","cs","csdx","v"}) {
        uint32_t bad[3]={1024,40,0}; bool detail=false;
        assert(!xtgvCardmarkArguments(text,bad,detail));
    }
    xtgvCardmarkRun("9 50 20 CSD",print,cancel);
    assert(mhz==40 && reads==8 && output.find("PASS: 13824")!=std::string::npos);
    assert(output.find(" CARDMARK: read-only RAW Arduino SdFat/SPI SD test;")==0);
    assert(output.find(" Arduino SPI CLK [MHz, approx]: 50 (restore 40, baseline 20)\r\n ... PASS:")!=std::string::npos);
    assert(output.find("Operating clock restored.")==std::string::npos);
    assert(output.find(" CID:") < output.find(" MID="));
    assert(output.find(" MID=") < output.find(" CSD:"));
    assert(output.find("Capacity=15,332 MiB (31399936 LBA 512B sectors)")!=std::string::npos);
    assert(output.find("0x32 -> 25.00 MHz")!=std::string::npos);
    assert(cmd6Calls==1 && output.find("50 MHz supported")!=std::string::npos);
    assert(output.find("CSD_STRUCTURE=1: v2.0")!=std::string::npos);
    assert(output.find("30663 (0x77C7)")!=std::string::npos);
    assert(xtgvCardmarkSpeedHz(0x5A)==50000000);
    assert(xtgvCardmarkSpeedHz(0xFF)==0 && xtgvCardmarkSpeedHz(0)==0);
    char grouped[27]; xtgvCardmarkNumber(UINT64_MAX,grouped);
    assert(std::string(grouped)=="18,446,744,073,709,551,615");
    output.clear(); cmd6OK=false;
    xtgvCardmarkRun("1",print,cancel);
    assert(mhz==40 && output.find("support unknown")!=std::string::npos);
    assert(output.find("PASS:")!=std::string::npos && output.find("CSD_STRUCTURE=")==std::string::npos);
    output.clear(); cmd6OK=true; highSpeed=false;
    xtgvCardmarkRun("1",print,cancel);
    assert(output.find("not advertised")!=std::string::npos);
    output.clear(); csdBytes[4]=0x1B; auto queries=cmd6Calls;
    xtgvCardmarkRun("1",print,cancel);
    assert(cmd6Calls==queries && output.find("check skipped")!=std::string::npos);
    csdBytes[4]=0x5B;
    for(unsigned version : {0U,2U,3U}) {
        output.clear(); auto old=csdBytes[0]; csdBytes[0]=version<<6;
        xtgvCardmarkCsd(csdBytes,print); csdBytes[0]=old;
        assert(output.find(version==0 ? "C_SIZE_MULT" : version==2 ? "v3.0 SDUC" : "reserved/unknown")!=std::string::npos);
        if(version>1) assert(output.find("READ_BL_LEN")==std::string::npos);
    }
    output.clear(); corrupt=true;
    xtgvCardmarkRun("8 50",print,cancel);
    assert(mhz==40 && output.find("MISMATCH sector 0 byte 17")!=std::string::npos);
    assert(output.find("FAIL:")!=std::string::npos);
    output.clear(); corrupt=false; readFail=true;
    xtgvCardmarkRun("8 50",print,cancel);
    assert(mhz==40 && output.find("Read error")!=std::string::npos);
    output.clear(); readFail=false; abortTest=true;
    xtgvCardmarkRun("8 50",print,cancel);
    assert(mhz==40 && output.find("CANCELLED:")!=std::string::npos);
    output.clear(); abortTest=false; auto previous=reads;
    xtgvCardmarkRun("8 50 31399935",print,cancel);
    assert(mhz==40 && reads==previous && output.find("Range exceeds")!=std::string::npos);
    output.clear(); restoreFail=true;
    bool stopped=false;
    try {xtgvCardmarkRun("8 50",print,cancel);} catch(int) {stopped=true;}
    assert(stopped && output.find("CLOCK RESTORE FAILED")!=std::string::npos);
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-cardmark-test-') as temp:
            binary = Path(temp) / 'test'
            targets = ('ESP32C6_LCD_1_47', 'JC8012P4A1C', 'JC8048W550', 'JC8048W535',
                       'RP2350_LCD_1_47_A', 'RP2350_TOUCH_LCD_2')
            variants = [(target, 0) for target in targets] + [('JC8012P4A1C', 1)]
            for target, read_only in variants:
                with self.subTest(target=target, read_only=read_only):
                    variant = source.replace('#define XTGV_STORAGE_READ_ONLY 1',
                                             f'#define XTGV_STORAGE_READ_ONLY {read_only}')
                    variant = variant.replace('RUNCPM_TARGET_JC8012P4A1C',
                                              f'RUNCPM_TARGET_{target}')
                    result = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra',
                        '-Werror', '-I', str(ROOT), '-x', 'c++', '-', '-o', str(binary)],
                        input=variant, text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    subprocess.run([str(binary)], check=True, timeout=5)

    def test_host_storage_has_no_raw_card(self):
        source = '''
#define RUNCPM_TARGET_JC8048W535 1
#define XTGV_STORAGE_HAS_RAW_CARD 0
#define SPI_DRIVER_SELECT 3
#include <cstdint>
#include "hardware/arduino_7xtgv/cardmark.h"
static_assert(XTGV_CARDMARK_AVAILABLE == 0, "Host storage has no raw card");
'''
        result = subprocess.run(['g++', '-std=c++17', '-I', str(ROOT),
                                 '-x', 'c++', '-fsyntax-only', '-'],
                                input=source, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
