"""Offline launcher/setup checks; no GUI, USB, installer or original-card writes."""
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import target_desktop
import target_mirror


class DesktopToolsTests(unittest.TestCase):
    def test_desktop_mirror_does_not_discover_hardware(self):
        with patch.object(sys, 'argv', ['mirror', '--target', 'jc8012p4a1c',
                '--desktop-pty', '--port', '/dev/pts/123', '--session', '7xtgv-desktop-test']), \
                patch.object(target_mirror, 'resolve_port') as resolve, \
                patch.object(target_mirror, 'select_mirror', return_value='kitty'), \
                patch.object(target_mirror.shutil, 'which', return_value='/usr/bin/tool'), \
                patch.dict(os.environ, {'XTGV_REFLASH_COMMAND': 'never-run-this'}), \
                patch.object(target_mirror.os, 'execve') as launch:
            target_mirror.main()
            resolve.assert_not_called()
            executable, argv, env = launch.call_args.args
            self.assertTrue(executable.endswith('/kit.sh'))
            self.assertEqual(argv[1:4], ['--desktop-pty', '--port', '/dev/pts/123'])
            self.assertEqual(env['XTGV_SESSION'], '7xtgv-desktop-test')
            self.assertNotIn('XTGV_REFLASH_COMMAND', env)

    def test_desktop_rejects_serial_port_and_shared_session(self):
        with patch.object(target_mirror.shutil, 'which', return_value='/usr/bin/tool'):
            for port, session in [('/dev/ttyACM0', '7xtgv-desktop-test'),
                                  ('/dev/pts/123', '7xtgv-cpm')]:
                with self.subTest(port=port, session=session), self.assertRaises(RuntimeError):
                    target_mirror.mirror_command('jc8012p4a1c', port, 'terminal', session)

    def test_auto_mirror_fallbacks(self):
        with patch.dict(os.environ, {'DISPLAY': ':1'}):
            for available, wanted in [({'konsole', 'kitty'}, 'konsole'), ({'kitty'}, 'kitty'), (set(), 'terminal')]:
                with patch.object(target_mirror.shutil, 'which', side_effect=lambda x: x if x in available else None):
                    self.assertEqual(target_mirror.select_mirror(), wanted)

    def test_root_selection_and_mount_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'cpmcard/A/0').mkdir(parents=True)
            with patch.dict(os.environ, {'XTGV_CARD_ROOT': str(root / 'cpmcard')}):
                self.assertEqual(target_desktop.card_root('local'), root / 'cpmcard')
            with self.assertRaisesRegex(RuntimeError, 'Not a mounted'):
                target_desktop.card_root('card', root / 'cpmcard')
            with patch.object(target_desktop.os.path, 'ismount', return_value=True), \
                    patch.dict(os.environ, {'XTGV_DESKTOP_CARD_ROOT': str(root / 'cpmcard')}):
                self.assertEqual(target_desktop.card_root('card'), root / 'cpmcard')

    def test_simple_launch_quiet_and_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); (root / 'A/0').mkdir(parents=True)
            argv = ['launch', 'local', '--target', 'jc8012p4a1c', '--card-root', str(root),
                    '--log', str(root / 'desktop-local.log')]
            output = io.StringIO()
            with patch.object(sys, 'argv', argv), patch.object(target_desktop.os, 'execv') as launch, \
                    contextlib.redirect_stdout(output):
                target_desktop.main()
            command = launch.call_args.args[1]
            self.assertIn('--quiet', command)
            self.assertIn(str(root), command)
            self.assertNotIn(str(root), output.getvalue())
            self.assertIn('read/write', output.getvalue())
            self.assertNotIn('reflash', ' '.join(command))

    def test_setup_desktop_package_selection_dry_run(self):
        for family, package in [('arch', 'sdl2'), ('debian', 'libsdl2-dev'), ('fedora', 'SDL2-devel')]:
            with self.subTest(family=family), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                release = root / 'os-release'; release.write_text('ID=' + family + '\n')
                script = root / 'setup.sh'
                script.write_text((TOOLS / 'setup.sh').read_text().replace(
                    'source /etc/os-release', f'source "{release}"'))
                result = subprocess.run(['bash', str(script), '--dry-run', '--with-desktop'],
                                        text=True, capture_output=True, check=True)
                self.assertIn(package, result.stdout)
                self.assertIn('cmake make', result.stdout)
                self.assertNotIn('Setup complete', result.stdout)

    def test_quiet_native_run_logs_details(self):
        sketch = TOOLS.parent
        binary = sketch.parents[1] / 'build/RunCPM_7xtgv-terminal/desktop/jc8012p4a1c/xtgv_desktop'
        if not binary.exists(): self.skipTest('Desktop binary not built on this machine')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); (root / 'A/0').mkdir(parents=True)
            log = root / 'desktop-local.log'
            result = subprocess.run([sys.executable, str(sketch / 'desktop/desktop.py'),
                'run', '--target', 'EP4_10', '--card-root', str(root), '--headless', '--no-mirror',
                '--run-for-ms', '300', '--quiet', '--log', str(log)],
                capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn(str(root), result.stdout)
            self.assertIn('Desktop stopped.', result.stdout)
            self.assertIn('\n\n*** DESKTOP ***', log.read_text())
            self.assertIn(str(root), log.read_text())


if __name__ == '__main__': unittest.main()
