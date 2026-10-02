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
#define XTGV_STORAGE_READ_ONLY 1
static unsigned mhz=40, reads=0;
static bool corrupt=false, readFail=false, abortTest=false, restoreFail=false;
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
    bool readCSD(csd_t*){return true;}
    uint32_t sectorCount(){return cardSectors();}
    uint8_t errorCode(){return cardError();}
    bool readSectors(uint32_t sector,uint8_t *p,size_t n){return readRaw(sector,p,n);}
    bool readIdentity(cid_t*,csd_t*){return true;}
    uint32_t cardSectors(){return 10000;}
    uint8_t cardError(){return 9;}
    bool readRaw(uint32_t sector,uint8_t *p,size_t n) {
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
    assert(xtgvCardmarkArguments(" 9 50 20 ",args));
    assert(args[0]==9 && args[1]==50 && args[2]==20);
    for(auto text:{"0","1 0","1 61","-1","4294967296","1 50 0 1","1 50x"}) {
        uint32_t bad[3]={1024,40,0}; assert(!xtgvCardmarkArguments(text,bad));
    }
    xtgvCardmarkRun("9 50 20",print,cancel);
    assert(mhz==40 && reads==8 && output.find("PASS: 13824")!=std::string::npos);
    assert(output.find("CID:")!=std::string::npos && output.find("CSD:")!=std::string::npos);
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
    xtgvCardmarkRun("8 50 9999",print,cancel);
    assert(mhz==40 && reads==previous && output.find("Range exceeds")!=std::string::npos);
    output.clear(); restoreFail=true;
    bool stopped=false;
    try {xtgvCardmarkRun("8 50",print,cancel);} catch(int) {stopped=true;}
    assert(stopped && output.find("CLOCK RESTORE FAILED")!=std::string::npos);
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-cardmark-test-') as temp:
            binary = Path(temp) / 'test'
            for read_only in (0, 1):
                with self.subTest(read_only=read_only):
                    variant = source.replace('#define XTGV_STORAGE_READ_ONLY 1',
                                             f'#define XTGV_STORAGE_READ_ONLY {read_only}')
                    result = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra',
                        '-Werror', '-I', str(ROOT), '-x', 'c++', '-', '-o', str(binary)],
                        input=variant, text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    subprocess.run([str(binary)], check=True, timeout=5)
