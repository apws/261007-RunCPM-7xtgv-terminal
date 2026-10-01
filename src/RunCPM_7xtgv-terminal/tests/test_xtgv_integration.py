"""Exercise the production integration helpers in terminal and serial modes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class XtgvIntegrationTests(unittest.TestCase):
    def compile_run(self, source, mode):
        with tempfile.TemporaryDirectory(prefix='xtgv-integration-') as tmp:
            cpp = Path(tmp) / 'test.cpp'
            binary = Path(tmp) / 'test'
            cpp.write_text(source)
            result = subprocess.run([
                'g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-variable', '-Wno-unused-function',
                '-I', str(ROOT), f'-DXTGV_TERMINAL={mode}',
                str(cpp), '-o', str(binary),
            ], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_benchmark_protocol_in_both_modes(self):
        abstraction = (ROOT / 'abstraction_arduino_7xtgv.h').read_text()
        begin = abstraction.index('static const uint8 XTGV_DRV_PORT')
        end = abstraction.index('/* Console abstraction functions */', begin)
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
using uint8 = uint8_t;
using uint32 = unsigned long;
using uint64 = unsigned long long;
static std::string printed;
static uint32 now = 0;
static bool paused = false;
static unsigned pauseCalls = 0;
static bool historyOK = true, configOK = true;
uint32 micros() { return now; }
void _putch(uint8 ch) { printed += char(ch); }
void terminalPause(bool value) { paused = value; ++pauseCalls; }
void terminalTextBenchmarkRequest() {}
void terminalScheduleReboot() {}
void terminalHistoryForceSave() {}
void terminalConfigForceSave() {}
bool terminalHistoryStoragePoll() { return historyOK; }
bool terminalConfigStoragePoll() { return configOK; }
bool terminalTextBenchmarkDone() { return true; }
uint32_t terminalTextBenchmarkElapsedUs() { return 1000; }
uint32_t terminalTextBenchmarkBytes() { return 100; }
uint32_t terminalTextBenchmarkChars() { return 100; }
''' + abstraction[begin:end] + r'''
int main() {
    _HardwareInit();
    assert(_HardwareIn(0x7b) == 'V');
    assert(_HardwareIn(0x7a) == 'D');
    assert(_HardwareIn(0x7e) == 'B');
    for (uint8 start : {uint8(1), uint8(3), uint8(9), uint8(11)}) {
        const bool quiet = start >= 9;
        printed.clear(); now = 100;
        _HardwareOut(0x7a, start);
        assert(xtgvBenchmarkRunning && paused == (quiet && XTGV_TERMINAL));
        now += 1000000;
        _HardwareOut(0x7a, start + 1);
        assert(!xtgvBenchmarkRunning && !paused);
        assert(printed.find("VTDRV ") == 0);
        assert((printed.find("quiet ") != std::string::npos) == quiet);
        assert(printed.find("32768 bytes in 1000000 us = 32.00 KiB/s") != std::string::npos);
    }
    _HardwareOut(0x7a, 11);
    _HardwareOut(0x7a, 0xff);
    assert(!xtgvBenchmarkRunning && !paused && !xtgvDrvQuiet);
    _HardwareOut(0x7b, 2);
    assert(xtgvBenchmarkRunning && paused == bool(XTGV_TERMINAL));
    now += 1000000;
    _HardwareOut(0x7b, 3);
    assert(!xtgvBenchmarkRunning && !paused);
    _HardwareOut(0x7d, 1);
    assert(_HardwareIn(0x7d) == (XTGV_TERMINAL ? 1u : 0xffu));
    configOK = false;
    _HardwareOut(0x7d, 1);
    assert(_HardwareIn(0x7d) == 0xff);
    if (!XTGV_TERMINAL) assert(pauseCalls == 0);
}
'''
        for mode in (0, 1):
            with self.subTest(terminal=mode):
                self.compile_run(harness, mode)

    def test_external_lifecycle_in_both_modes(self):
        ccp = (ROOT / 'ccp7xf.h').read_text()
        begin = ccp.index('#if XTGV_TERMINAL\nstatic bool _xtgvCancelForcedSubmit')
        end = ccp.index('// External (.COM) command', begin)
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <string>
#define XTGV_CCP_7XF 1
#include "hardware/arduino_7xtgv/runcpm_hooks.h"
using uint8 = uint8_t;
constexpr bool FALSE = false;
static uint8 cDrive = 1, userCode = 7;
static bool submitFlag = true, xtgvCcpIgnoreStrandedSubmit = false;
static unsigned submitRecords = 3;
static uint8 xtgvCcpStrandedSubmitPath[sizeof("A/0/$$$.SUB")] = {};
static bool exists = true, deleteOK = true, acknowledge = true;
static unsigned waits = 0, discards = 0;
static std::string printed, deleted;
char tohex(uint8 value) { return "0123456789ABCDEF"[value & 15]; }
bool _sys_exists(uint8 *) { return exists; }
bool _sys_deletefile(uint8 *path) { deleted = (const char *)path; return deleteOK; }
void _puts(const char *text) { printed += text; }
void delay(unsigned value) {
    waits += value;
    if (acknowledge && waits == 2) forcedWarmReturn().terminalInputCleaned();
}
void terminalDiscardGuestInput() { ++discards; }
''' + ccp[begin:end] + r'''
int main() {
    _xtgvExternalBegin();
    _xtgvExternalEnd();
    assert(printed.empty() && discards == 0 && deleted.empty());
#if XTGV_TERMINAL
    for (bool failure : {false, true}) {
        printed.clear(); waits = 0;
        deleteOK = acknowledge = !failure;
        _xtgvExternalBegin();
        assert(forcedWarmReturn().request());
        assert(XTGV_ABORT_REQUESTED());
        _xtgvExternalEnd();
        assert(!forcedWarmReturn().suppressInput());
        assert(!submitFlag && submitRecords == 0);
        assert(deleted == "B/7/$$$.SUB");
        assert(xtgvCcpIgnoreStrandedSubmit == failure);
        assert(printed.find("\r\nFORCED WARM RETURN\r\n") == 0);
        assert(waits == (failure ? 200u : 2u));
        if (failure) {
            assert(!strcmp((char *)xtgvCcpStrandedSubmitPath, "B/7/$$$.SUB"));
            assert(printed.find("BATCH HELD") != std::string::npos);
        }
    }
#else
    assert(!forcedWarmReturn().externalComActive());
    assert(submitFlag && submitRecords == 3);
#endif
}
'''
        for mode in (0, 1):
            with self.subTest(terminal=mode):
                self.compile_run(harness, mode)


if __name__ == '__main__':
    unittest.main()
