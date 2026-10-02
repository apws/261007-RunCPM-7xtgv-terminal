"""Project additions must not replace board/core build flags."""
from pathlib import Path
import subprocess
import json
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import reflash


class BuildPropertiesTests(unittest.TestCase):
    def test_all_sdfat_targets_select_external_spi_driver(self):
        for target in ('jc8012p4a1c', 'jc8048w535', 'jc8048w550',
                       'rp2350-lcd-1-47-a', 'rp2350-touch-lcd-2'):
            with self.subTest(target=target):
                definition = json.loads((Path(__file__).resolve().parents[1] /
                    f'tools/targets/{target}.json').read_text())
                self.assertIn('-DSPI_DRIVER_SELECT=3',
                              definition['build_properties']['build.extra_flags'])

    def test_p4_array_transfers_are_build_wide(self):
        path = Path(__file__).resolve().parents[1] / 'tools/targets/jc8012p4a1c.json'
        definition = json.loads(path.read_text())
        result = subprocess.CompletedProcess([], 0, 'build.extra_flags=-DCORE\n', '')
        with patch.object(reflash.subprocess, 'run', return_value=result):
            actual = reflash.resolved_build_properties('cli', definition, 'stamp')
        self.assertIn('-DUSE_SPI_ARRAY_TRANSFER=1', actual['build.extra_flags'].split())

    def test_preserves_defaults_quotes_and_other_overrides(self):
        base = ('-DARDUINO_HOST_OS="linux" -DESP32=ESP32 -DBOARD_HAS_PSRAM '
                '-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 '
                '-DARDUINO_RUNNING_CORE=1 -DARDUINO_EVENT_RUNNING_CORE=1')
        definition = {'profile': 'example', 'build_properties': {
            'build.extra_flags': '-DPROJECT_OPTION', 'build.code_debug': '2'}}
        result = subprocess.CompletedProcess([], 0, 'other=x\nbuild.extra_flags=' + base + '\n', '')
        with patch.object(reflash.subprocess, 'run', return_value=result) as query:
            actual = reflash.resolved_build_properties('cli', definition, '260924-1500')
        self.assertEqual(actual['build.extra_flags'], base + ' -DPROJECT_OPTION -DXTGV_BUILD_STAMP=260924-1500')
        self.assertEqual(actual['build.code_debug'], '2')
        command = query.call_args.args[0]
        self.assertIn('build.code_debug=2', command)
        self.assertFalse(any(arg.startswith('build.extra_flags=') for arg in command))
        self.assertEqual(definition['build_properties']['build.extra_flags'], '-DPROJECT_OPTION')

    def test_empty_defaults_are_valid(self):
        result = subprocess.CompletedProcess([], 0, 'build.extra_flags=\n', '')
        with patch.object(reflash.subprocess, 'run', return_value=result):
            self.assertEqual(reflash.resolved_build_properties('cli', {'profile': 'p'}, 'stamp'),
                             {'build.extra_flags': '-DXTGV_BUILD_STAMP=stamp'})

    def test_optimization_override_reaches_property_query_and_build(self):
        result = subprocess.CompletedProcess([], 0, 'build.extra_flags=-DCORE\n', '')
        with patch.object(reflash.subprocess, 'run', return_value=result) as query:
            actual = reflash.resolved_build_properties(
                'cli', {'profile': 'p'}, 'stamp', {'build.flags.optimize': '-O2'})
        self.assertEqual(actual['build.flags.optimize'], '-O2')
        self.assertEqual(actual['build.extra_flags'], '-DCORE -DXTGV_BUILD_STAMP=stamp')
        self.assertIn('build.flags.optimize=-O2', query.call_args.args[0])

    def test_esp32_optimization_override_reaches_query_and_build(self):
        result = subprocess.CompletedProcess([], 0, 'build.extra_flags=-DCORE\n', '')
        for level in ('-O2', '-O3'):
            with self.subTest(esp32=level), patch.object(reflash.subprocess, 'run', return_value=result) as query:
                actual = reflash.resolved_build_properties(
                    'cli', {'profile': 'jc8012p4a1c'}, 'stamp',
                    {'compiler.optimization_flags': level})
                self.assertEqual(actual['compiler.optimization_flags'], level)
                self.assertIn('compiler.optimization_flags=' + level, query.call_args.args[0])

    def test_invalid_property_queries_stop_build(self):
        for code, output in ((1, ''), (0, 'unrelated=x\n'),
                             (0, 'build.extra_flags={unresolved}\n'),
                             (0, 'build.extra_flags=a\nbuild.extra_flags=b\n')):
            with self.subTest(code=code, output=output), patch.object(
                    reflash.subprocess, 'run', return_value=subprocess.CompletedProcess(
                        [], code, output, 'query failure')):
                with self.assertRaises(RuntimeError):
                    reflash.resolved_build_properties('cli', {'profile': 'p'}, 'stamp')


if __name__ == '__main__':
    unittest.main()
