"""Check host labels against actual compiled HAL selectors, without hardware."""
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TargetMetadataTests(unittest.TestCase):
    def test_arduino_builds_select_exactly_one_board(self):
        source = '''#include "runcpm-config.h"
#if XTGV_RUNCPM != 1 || XTGV_TERMINAL != 1 || XTGV_CCP_7XF != 1
#error "Display target must select integrated mode"
#endif
#ifdef RUNCPM_HEADLESS
#error "Integrated mode must not be headless"
#endif
'''
        command = ['g++', '-std=c++17', '-fsyntax-only', '-DARDUINO',
                   '-I', str(ROOT / 'tests/stubs'), '-I', str(ROOT),
                   '-x', 'c++', '-']
        targets = sorted((ROOT / 'tools/targets').glob('*.json'))
        flags_by_target = []
        for path in targets:
            data = json.loads(path.read_text())
            flags = shlex.split(data.get('build_properties', {}).get('build.extra_flags', ''))
            self.assertEqual(len([flag for flag in flags
                                  if flag.startswith('-DRUNCPM_TARGET_')]), 1, path.stem)
            flags_by_target.append(flags)
            with self.subTest(target=path.stem):
                subprocess.run(command[:1] + flags + command[1:], input=source,
                               text=True, check=True, capture_output=True)
        for flags in ([], flags_by_target[0] + flags_by_target[1]):
            with self.subTest(invalid=flags):
                result = subprocess.run(command[:1] + flags + command[1:],
                                        input=source, text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Select exactly one RunCPM board target', result.stderr)

    def test_serial_default_and_unimplemented_modes(self):
        command = ['g++', '-std=c++17', '-fsyntax-only', '-DARDUINO',
                   '-DRUNCPM_TARGET_RP2350_TOUCH_LCD_2',
                   '-I', str(ROOT / 'tests/stubs'), '-I', str(ROOT),
                   '-x', 'c++', '-']
        serial = '''#include "runcpm-config.h"
#if XTGV_RUNCPM != 1 || XTGV_TERMINAL != 0 || XTGV_CCP_7XF != 0
#error "Direct target build must default to serial RunCPM"
#endif
#ifndef RUNCPM_HEADLESS
#error "Serial RunCPM must retain the legacy headless selector"
#endif
'''
        subprocess.run(command, input=serial, text=True, check=True, capture_output=True)
        for flags, expected in (
            (['-DXTGV_RUNCPM=0', '-DXTGV_TERMINAL=1'], 'reserved, not implemented'),
            (['-DXTGV_RUNCPM=0'], 'Enable XTGV_RUNCPM or XTGV_TERMINAL'),
            (['-DXTGV_TERMINAL=2'], 'must be 0 or 1'),
            (['-DXTGV_CCP_7XF=2'], 'must be 0 or 1'),
            (['-DXTGV_TERMINAL=1', '-DRUNCPM_HEADLESS'], 'conflicts with XTGV_TERMINAL=1'),
        ):
            with self.subTest(flags=flags):
                result = subprocess.run(command[:1] + flags + command[1:],
                                        input='#include "runcpm-config.h"\n',
                                        text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, result.stderr)

    def test_ccp_variants_have_separate_ownership(self):
        plain = (ROOT / 'ccp.h').read_text()
        extended = (ROOT / 'ccp7xf.h').read_text()
        sketch = (ROOT / 'RunCPM_7xtgv-terminal.ino').read_text()
        self.assertNotIn('ccp_catalog.h', plain)
        self.assertNotIn('XTGV_CCP_7XF', plain)
        self.assertIn('#include "ccp_catalog.h"', extended)
        self.assertIn('#include "ccp7xf.h"', sketch)
        self.assertIn('#include "ccp.h"', sketch)

    def test_boot_storage_policy_is_board_owned(self):
        sketch = (ROOT / 'RunCPM_7xtgv-terminal.ino').read_text()
        board = (ROOT / 'hardware/arduino_7xtgv/board.h').read_text()
        self.assertNotIn('RUNCPM_TARGET_', sketch)
        self.assertNotIn('TODO HAL/BOARD', sketch)
        self.assertIn('xtgvBoardStorageInit()', sketch)
        self.assertIn('xtgvBoardPrintStorageStatus(storage, _puts)', sketch)
        self.assertIn('xtgvBoardPrintBootExtra(_puts)', sketch)
        self.assertIn('struct XtgvBoardStorageStatus', board)
        for name in ('jc8048w535_sdmmc.h', 'jc8048w550_sd_spi.h',
                     'pico_rp2350_lcd_1_47_a_sd_spi.h',
                     'pico_rp2350_touch_lcd_2_sd_spi.h'):
            with self.subTest(board=name):
                policy = (ROOT / 'hardware/arduino_7xtgv/boards' / name).read_text()
                self.assertIn('XTGV_BOARD_BOOT_EXTRA', policy)
                self.assertIn('XTGV_BOARD_STORAGE_INIT_LABEL', policy)

    def test_compiled_identity_and_display_group_match_host_data(self):
        # Host substitutes for architecture macros supplied by these Arduino cores.
        profiles = {
            'jc8048w550': ['-DARDUINO_ARCH_ESP32', '-DCONFIG_IDF_TARGET_ESP32S3'],
            'jc8048w535': ['-DARDUINO_ARCH_ESP32', '-DCONFIG_IDF_TARGET_ESP32S3'],
            'rp2350-touch-lcd-2': ['-DARDUINO_ARCH_RP2040'],
            'rp2350-lcd-1-47-a': ['-DARDUINO_ARCH_RP2040'],
        }
        source = r'''
#include <cstdio>
#include <cassert>
#include "src/terminal/display_policy.h"
#include "src/terminal/terminal.h"
#include "hardware/arduino_7xtgv/target_identity.h"
int main() {
    const auto &hal = vt::displayHalDefaults();
    assert(hal.logicalMaxCols == vt::MaxCols && hal.logicalMaxRows == vt::MaxRows);
    assert(hal.diskIndicatorYOffset + 7 <= hal.physicalHeight);
#if !defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
    assert(hal.diskIndicatorYOffset == 0);
    assert(hal.bootWarning == nullptr);
#endif
#if defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
    assert(hal.physicalWidth == 320 && hal.physicalHeight == 172);
    assert(hal.diskIndicatorYOffset == 10);
    assert(hal.bootWarning && hal.bootWarning[0]);
    assert(hal.zoomGeometry[0].cols == 80 && hal.zoomGeometry[0].rows == 16);
    assert(vt::evenVisibleCols(hal.physicalWidth, 1) == 52);
#if defined(RUNCPM_TARGET_RP2350_TOUCH_LCD_2)
#error "1.47-inch target must not fall through to the 2-inch selector"
#endif
#endif
    for (int i = 0; i < 4; ++i) {
        const auto &g = hal.zoomGeometry[i];
        assert(g.cols >= 2 && !(g.cols & 1) && g.cols <= hal.logicalMaxCols);
        assert(g.rows >= 1 && g.rows <= hal.logicalMaxRows);
        assert(g.rows * 10 * (i + 1) <= hal.physicalHeight);
    }
    std::printf("%s\n%s\n", xtgvFirmwareTarget(), hal.id);
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-target-metadata-') as temp:
            binary = Path(temp) / 'identity'
            for path in sorted((ROOT / 'tools/targets').glob('*.json')):
                data = json.loads(path.read_text())
                flags = shlex.split(data.get('build_properties', {}).get('build.extra_flags', ''))
                with self.subTest(target=path.stem):
                    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ROOT), *profiles[data['profile']], *flags,
                                    '-x', 'c++', '-', '-o', str(binary)],
                                   input=source, text=True, check=True, capture_output=True)
                    result = subprocess.check_output([str(binary)], text=True).splitlines()
                    self.assertEqual(result, [path.stem, data['display_id']])


if __name__ == '__main__':
    unittest.main()
