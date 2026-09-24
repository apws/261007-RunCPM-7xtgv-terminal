"""Project additions must not replace board/core build flags."""
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import reflash


class BuildPropertiesTests(unittest.TestCase):
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

    def test_query_errors_or_unresolved_defaults_stop_build(self):
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
