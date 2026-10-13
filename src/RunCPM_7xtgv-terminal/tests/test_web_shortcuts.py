"""Quiet web shortcut transcripts, stage failures and launch exit status."""
import contextlib
import io
from pathlib import Path
import shlex
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

WEB = Path(__file__).resolve().parents[1] / 'tools-web'
sys.path.insert(0, str(WEB))
import shortcut


class WebShortcutTests(unittest.TestCase):
    def test_launch_details_only_in_log(self):
        log, output = io.StringIO(), io.StringIO()
        script = ('import sys; print("URL: http://127.0.0.1:1234/"); '
                  'print("GET /asset.wasm HTTP/1.1 200", file=sys.stderr); '
                  'print("browser launcher warning", file=sys.stderr)')
        with contextlib.redirect_stdout(output):
            shortcut.serve([sys.executable, '-c', script], log)
        self.assertIn('http://127.0.0.1:1234/', output.getvalue())
        self.assertIn('Browser hosting stopped.', output.getvalue())
        for detail in ('GET /asset.wasm', 'browser launcher warning'):
            self.assertIn(detail, log.getvalue())
            self.assertNotIn(detail, output.getvalue())

    def test_launch_failure_keeps_exit_status_and_trace_in_adjacent_log(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'browser.py').write_text('raise SystemExit("fixture launch error")\n')
            output, errors = io.StringIO(), io.StringIO()
            with patch.object(shortcut, 'HERE', root), contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                self.assertEqual(shortcut.main(['local', '--no-open']), 1)
            log = (root / 'desktop-local.log').read_text()
            self.assertIn('fixture launch error', log)
            self.assertIn('Traceback', log)
            self.assertIn('See desktop-local.log', errors.getvalue())
            self.assertNotIn('fixture launch error', output.getvalue() + errors.getvalue())
            self.assertNotIn('Browser hosting stopped.', output.getvalue())

    def test_missing_sdk_fails_without_install_or_build(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with patch.object(shortcut, 'HERE', root), patch.object(shortcut, 'BUILD', root/'build'), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(shortcut.main(['rebuild']), 1)
            self.assertIn('setup.sh first', (root/'desktop-rebuild.log').read_text())
            self.assertFalse((root/'build').exists())

    @unittest.skipUnless(shutil.which('bash'), 'SDK activation uses Bash')
    def test_build_progress_stage_failure_and_packaging(self):
        for code in (7, 0):
            with self.subTest(code=code), tempfile.TemporaryDirectory(prefix='web shortcut ') as folder:
                root = Path(folder); build = root/'build'; binaries = root/'bin'; binaries.mkdir()
                sdk = build/'_deps/emsdk/emsdk_env.sh'; sdk.parent.mkdir(parents=True)
                sdk.write_text('export PATH=' + shlex.quote(str(binaries)) + ':"$PATH"\n')
                for name, script in [('emcmake', 'print("configure detail")'), ('cmake', f'print("[ 25%] compile\\ncompiler warning\\n[100%] link"); raise SystemExit({code})')]:
                    path = binaries/name
                    path.write_text('#!' + sys.executable + '\n' + script + '\n'); path.chmod(0o755)
                (root/'build_assets.py').write_text('print("package detail")\n')
                output, errors = io.StringIO(), io.StringIO()
                with patch.object(shortcut, 'HERE', root), patch.object(shortcut, 'BUILD', build), contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                    self.assertEqual(shortcut.main(['rebuild']), code)
                log = (root/'desktop-rebuild.log').read_text()
                self.assertIn('configure detail', log); self.assertIn('compiler warning', log)
                self.assertNotIn('compiler warning', output.getvalue())
                self.assertIn('Building browser:  25%', output.getvalue())
                if code:
                    self.assertNotIn('100%', output.getvalue())
                    self.assertNotIn('package detail', log)
                    self.assertIn('See desktop-rebuild.log', errors.getvalue())
                else:
                    self.assertIn('Building browser: 100%', output.getvalue())
                    self.assertIn('Browser build: OK', output.getvalue())
                    self.assertIn('package detail', log)


if __name__ == '__main__':
    unittest.main()
