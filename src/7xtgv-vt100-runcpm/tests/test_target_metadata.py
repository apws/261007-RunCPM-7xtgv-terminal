"""Check host labels against actual compiled HAL selectors, without hardware."""
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TargetMetadataTests(unittest.TestCase):
    def test_compiled_identity_and_display_group_match_host_data(self):
        # Host substitutes for architecture macros supplied by these Arduino cores.
        profiles = {
            'jc8048w550': ['-DARDUINO_ARCH_ESP32', '-DCONFIG_IDF_TARGET_ESP32S3'],
            'jc8048w535': ['-DARDUINO_ARCH_ESP32', '-DCONFIG_IDF_TARGET_ESP32S3'],
            'rp2350-touch-lcd-2': ['-DARDUINO_ARCH_RP2040'],
        }
        source = r'''
#include <cstdio>
#include <cassert>
#include "src/terminal/display_policy.h"
#include "src/terminal/terminal.h"
#include "hardware/arduino_7xtgv/target_identity.h"
int main() {
    const auto &hal = vt::displayHalDefaults();
    assert(hal.maxCols == vt::MaxCols && hal.maxRows == vt::MaxRows);
    for (int i = 0; i < 4; ++i) {
        const auto &g = hal.zoomGeometry[i];
        assert(g.cols >= 2 && !(g.cols & 1) && g.cols <= hal.maxCols);
        assert(g.rows >= 1 && g.rows <= hal.maxRows);
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
