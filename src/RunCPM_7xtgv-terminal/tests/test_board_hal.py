"""Compile board definitions once and link ordinary calls from another TU."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BoardHalTests(unittest.TestCase):
    def test_each_board_links_across_translation_units(self):
        owner = r'''
#define XTGV_HAL_IMPLEMENTATION
#define XTGV_TERMINAL 1
#include "hardware/arduino_7xtgv/board.h"
#include <cassert>
#if defined(RUNCPM_TARGET_JC8012P4A1C)
static_assert(XTGV_DISPLAY_FULL_FRAME_BUDGET);
#endif
#if defined(RUNCPM_TARGET_JC8048W535) && !defined(XTGV_W535_SD_MMC)
static_assert(XTGV_STORAGE_HZ == 20000000UL);
#endif
int client();
int main() {
    expectedBacklightPin = XTGV_BACKLIGHT_PIN;
    xtgvBoardBacklightInit();
    xtgvBoardBacklightWrite(0);
    auto *display = xtgvBoardCreateDisplay();
    assert(display && xtgvBoardBeginDisplay(display));
    assert(client() == XTGV_TERMINAL_ROWS);
    assert(backlight == 123);
    delete display;
    delete gfxTestBus;
}
'''
        client = r'''
#include "hardware/arduino_7xtgv/board.h"
#include "src/terminal/display_policy.h"
#include "src/terminal/terminal.h"
static_assert(vt::MaxRows == XTGV_TERMINAL_ROWS);
int client() {
    xtgvBoardBacklightWrite(123);
    return vt::displayHalDefaults().logicalMaxRows;
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-hal-link-') as temp:
            temp = Path(temp)
            (temp / 'owner.cpp').write_text(owner)
            (temp / 'client.cpp').write_text(client)
            variants = (
                ('JC8012P4A1C', ()),
                ('JC8048W535', ()),
                ('JC8048W535_SD_MMC', ('-DRUNCPM_TARGET_JC8048W535',
                                       '-DXTGV_W535_SD_MMC')),
                ('JC8048W550', ()),
                ('RP2350_LCD_1_47_A', ()),
                ('RP2350_TOUCH_LCD_2', ()),
            )
            for target, extra_flags in variants:
                with self.subTest(target=target):
                    binary = temp / target
                    target_flags = list(extra_flags) or [f'-DRUNCPM_TARGET_{target}']
                    result = subprocess.run([
                        'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        *target_flags,
                        '-I', str(ROOT / 'tests/stubs'), '-I', str(ROOT),
                        str(temp / 'owner.cpp'), str(temp / 'client.cpp'),
                        '-o', str(binary),
                    ], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
