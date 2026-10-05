import unittest
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


class RunCPMCoreHooksTests(unittest.TestCase):
    def test_all_cpu_models_with_and_without_abort(self):
        # Real recovered core and POSIX abstraction: no Arduino/display mocks.
        # A HALT must still terminate normally; an endless JP must be stopped
        # by the optional callback after the same 65,536-opcode poll interval.
        source = r'''
#include <assert.h>
#include <stdint.h>
#include "globals.h"
#include "abstraction_posix.h"
FILE *pun_dev;
int pun_open = FALSE;
FILE *lst_dev;
int lst_open = FALSE;
#include "ram.h"
#include "console.h"
static unsigned polls = 0;
#if TEST_ABORT
static bool testAbort(void) { ++polls; return true; }
#define XTGV_ABORT_REQUESTED testAbort
#endif
#include TEST_CPU
#include "disk.h"
#include "host.h"
#include "cpm.h"
int main(void) {
    Z80reset();
    PC = 0x100;
    _RamWrite(0x100, 0x76); /* HALT: no callback before the poll interval */
    Z80run(0);
    assert(Status == STATUS_EXIT && polls == 0);
#if TEST_ABORT
    Z80reset();
    PC = 0x100;
    /* INC BC; JP 0100h: 65,535 executed instructions before first poll. */
    BC = 0;
    _RamWrite(0x100, 0x03);
    _RamWrite(0x101, 0xc3);
    _RamWrite(0x102, 0x00);
    _RamWrite(0x103, 0x01);
    Z80run(0);
    assert(Status == STATUS_RESTART && polls == 1);
    assert(BC == 32768);
#endif
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-core-hooks-') as tmp:
            path = Path(tmp) / 'core.c'
            binary = Path(tmp) / 'core'
            path.write_text(source)
            for cpu in range(1, 5):
                for enabled in (0, 1):
                    with self.subTest(cpu=cpu, abort=enabled):
                        result = subprocess.run([
                            'gcc', '-std=gnu11', '-O1', '-I', str(ROOT),
                            '-I', str(ROOT.parent / 'RunCPM_linux'),
                            f'-DTEST_CPU="cpu{cpu}.h"', f'-DTEST_ABORT={enabled}',
                            str(path), '-o', str(binary),
                        ], text=True, capture_output=True)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        subprocess.run([str(binary)], check=True, timeout=5)

    def test_cpu_loops_use_optional_abort_hook(self):
        for number in range(1, 5):
            with self.subTest(cpu=number):
                source = (ROOT / f'cpu{number}.h').read_text()
                self.assertIn('#ifdef XTGV_ABORT_REQUESTED', source)
                self.assertIn('XTGV_ABORT_REQUESTED()', source)
                self.assertNotIn('forcedWarmReturn()', source)

    def test_console_and_disk_hooks_are_product_neutral(self):
        console = (ROOT / 'cpm.h').read_text()
        disk = (ROOT / 'disk.h').read_text()
        self.assertEqual(console.count('if (XTGV_ABORT_REQUESTED())'), 3)
        self.assertNotIn('forcedWarmReturn()', console)
        self.assertIn('XTGV_MODIFIED_FILE_CLOSED((const char *)filename);', disk)
        self.assertNotIn('terminalConfigFileClosed', disk)

    def test_cpu_attributes_are_target_supplied(self):
        cpu = (ROOT / 'cpu1.h').read_text()
        hooks = (ROOT / 'hardware/arduino_7xtgv/runcpm_hooks.h').read_text()
        board = (ROOT / 'hardware/arduino_7xtgv/boards/jc8048w535.h').read_text()
        self.assertIn('#ifdef XTGV_Z80RUN_ATTRIBUTES', cpu)
        self.assertNotIn('RUNCPM_TARGET_JC8048W535', cpu)
        self.assertIn('#define XTGV_Z80RUN_ATTRIBUTES IRAM_ATTR', hooks)
        self.assertNotIn('optimize("O2")', hooks)
        self.assertNotIn('XTGV_CPU1_O2', board)

    def test_esp32_iram_is_independent_of_o2(self):
        for arch in ('ESP32', 'RP2040'):
            for o2 in (0, 1):
                with self.subTest(arch=arch, o2=o2):
                    source = '''
#define IRAM_ATTR TEST_IRAM
#include "hardware/arduino_7xtgv/runcpm_hooks.h"
#ifdef XTGV_Z80RUN_ATTRIBUTES
XTGV_Z80RUN_ATTRIBUTES
#else
NO_ATTRIBUTES
#endif
'''
                    result = subprocess.run([
                        'g++', '-E', '-P', '-x', 'c++', '-', '-I', str(ROOT),
                        f'-DARDUINO_ARCH_{arch}', f'-DXTGV_CPU1_O2={o2}',
                        '-DXTGV_TERMINAL=0', '-DXTGV_CCP_7XF=0',
                    ], input=source, text=True, capture_output=True, check=True)
                    if arch == 'ESP32':
                        self.assertIn('TEST_IRAM', result.stdout)
                        self.assertNotIn('optimize("O2")', result.stdout)
                    else:
                        self.assertIn('NO_ATTRIBUTES', result.stdout)


if __name__ == '__main__':
    unittest.main()
