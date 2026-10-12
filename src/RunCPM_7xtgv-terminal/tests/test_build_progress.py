"""Build output filtering, failure propagation and real Make/Ninja status."""
import contextlib
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'desktop'))
from build_progress import run_build


class BuildProgressTests(unittest.TestCase):
    def test_progress_is_monotonic_and_details_stay_in_log(self):
        lines = ['[ 10%] /private/source.cpp', 'compiler warning: detail',
                 '\x1b[32m[3/4] Building object\x1b[0m', '[ 20%] delayed status',
                 '[4/4] Linking executable']
        script = 'import os; assert os.environ["NINJA_STATUS"] == "[%f/%t] "; '
        script += 'print(' + repr('\n'.join(lines)) + ')'
        log, output = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(output):
            run_build([sys.executable, '-c', script], log)
        self.assertEqual(log.getvalue(), '\n'.join(lines) + '\n')
        self.assertNotIn('/private', output.getvalue())
        self.assertNotIn('compiler warning', output.getvalue())
        self.assertIn(' 75%', output.getvalue())
        self.assertNotIn(' 20%', output.getvalue())
        self.assertTrue(output.getvalue().endswith('100%\n'))

    def test_failure_never_reports_completion_and_keeps_diagnostic(self):
        log, output = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(output):
            with self.assertRaises(subprocess.CalledProcessError) as raised:
                run_build([sys.executable, '-c',
                           'print("[100%] Linking\\nfatal: fixture"); raise SystemExit(7)'], log)
        self.assertEqual(raised.exception.returncode, 7)
        self.assertNotIn('100%', output.getvalue())
        self.assertIn('fatal: fixture', log.getvalue())

    def test_terminal_rewrites_one_line(self):
        class Terminal(io.StringIO):
            def isatty(self):
                return True
        output = Terminal()
        with contextlib.redirect_stdout(output):
            run_build([sys.executable, '-c', 'print("[1/2] compile")'], io.StringIO())
        self.assertIn('\rBuilding desktop:  50%', output.getvalue())
        self.assertEqual(output.getvalue().count('\n'), 1)

    def test_real_make_and_ninja_build_and_incremental_noop(self):
        if not shutil.which('cmake'):
            self.skipTest('CMake not installed')
        for generator, tool in [('Unix Makefiles', 'make'), ('Ninja', 'ninja')]:
            if not shutil.which(tool):
                continue
            with self.subTest(generator=generator), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                (root / 'CMakeLists.txt').write_text(
                    'cmake_minimum_required(VERSION 3.19)\n'
                    'project(progress_fixture NONE)\n'
                    'add_custom_command(OUTPUT done.txt\n'
                    ' COMMAND ${CMAKE_COMMAND} -E touch done.txt)\n'
                    'add_custom_target(fixture ALL DEPENDS done.txt)\n')
                subprocess.run(['cmake', '-S', temp, '-B', str(root/'out'), '-G', generator],
                               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                for _ in range(2):
                    log, output = io.StringIO(), io.StringIO()
                    with contextlib.redirect_stdout(output):
                        run_build(['cmake', '--build', str(root/'out')], log)
                    self.assertTrue((root/'out/done.txt').is_file())
                    self.assertIn('100%', output.getvalue())
                    self.assertNotIn(temp, output.getvalue())
                    self.assertTrue(log.getvalue())


if __name__ == '__main__':
    unittest.main()
