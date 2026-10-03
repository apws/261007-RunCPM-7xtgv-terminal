"""Check corrected boot-test bytes and accounting with a tiny test interpreter."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BootClockTests(unittest.TestCase):
    def test_arduino_loop_timing_and_reset(self):
        source = r'''
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <string>
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = unsigned long long;
#define ARDUINO 1
static uint8 ram[32];
#define PUT_BYTE(a,v) (ram[a]=(v))
static constexpr int STATUS_RUNNING=0;
static int Status=STATUS_RUNNING;
static unsigned resets=0, pc=0, bc=0, de=0, a=0;
static bool zero=false;
static uint64 ticks=0;
static std::string output;
void _puts(const char *s) { output+=s; }
void Z80reset() { ++resets; pc=bc=de=a=0; zero=false; Status=STATUS_RUNNING; }
unsigned millis() { static unsigned t=0; unsigned result=t; t+=1000; return result; }
void Z80run(unsigned delay) {
    assert(delay==0);
    while(Status==STATUS_RUNNING) {
        switch(ram[pc++]) {
        case 0x11: de=ram[pc] | (ram[pc+1]<<8); pc+=2; ticks+=10; break;
        case 0x01: bc=ram[pc] | (ram[pc+1]<<8); pc+=2; ticks+=10; break;
        case 0x0b: bc=(bc-1)&65535; ticks+=6; break;
        case 0x1b: de=(de-1)&65535; ticks+=6; break;
        case 0x78: a=bc>>8; ticks+=4; break;
        case 0x7a: a=de>>8; ticks+=4; break;
        case 0xb1: a|=bc&255; zero=(a==0); ticks+=4; break;
        case 0xb3: a|=de&255; zero=(a==0); ticks+=4; break;
        case 0x20: {
            int displacement=int8_t(ram[pc++]);
            if(!zero) { pc+=displacement; ticks+=12; } else ticks+=7;
            break;
        }
        case 0x76: ticks+=4; Status=1; break;
        default: assert(false);
        }
    }
}
#include "cpu_mhz_coorect.h"
int main() {
    xtgvZ80estimateClock();
    assert(ticks==13015509ULL);
    assert(resets==2 && Status==STATUS_RUNNING && pc==0);
    assert(output=="13015509 T-states in 1000 ms\r\nEstimated Z80 clock speed: 13 MHz\r\n");
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-boot-clock-') as temp:
            binary = Path(temp) / 'test'
            result = subprocess.run(
                ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT),
                 '-x', 'c++', '-', '-o', str(binary)],
                input=source, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=10)
        sketch = (ROOT / 'RunCPM_7xtgv-terminal.ino').read_text()
        self.assertIn('xtgvZ80estimateClock();', sketch)
        self.assertNotIn('Run VTCPU.COM for active/quiet Z80 speed', sketch)
