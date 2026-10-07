"""Desktop owner lifecycle checks using disposable processes, without GUI or cards."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

DESKTOP = Path(__file__).resolve().parents[1] / 'desktop/desktop.py'
spec = importlib.util.spec_from_file_location('desktop_launcher', DESKTOP)
desktop = importlib.util.module_from_spec(spec)
spec.loader.exec_module(desktop)


class DesktopCloseTests(unittest.TestCase):
    def check_close(self, mirror_first, reboot=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child_path = root / 'xtgv_desktop'
            child_path.write_text(
                '#!' + sys.executable + '\n'
                'import signal, time, pathlib\n'
                'def stop(signum, frame):\n'
                '    pathlib.Path(' + repr(str(root / 'saved')) + ').write_text("saved")\n'
                '    raise SystemExit(' + ('75' if reboot else '0') + ')\n'
                'signal.signal(signal.SIGTERM, stop)\n'
                'print("PTY=/dev/pts/fake", flush=True)\n'
                'print("partial output", end="", flush=True)\n'
                + ('time.sleep(4)\nraise SystemExit(99)\n' if mirror_first
                   else 'time.sleep(0.15)\nstop(None, None)\n'))
            child_path.chmod(0o755)
            mirror_script = root / 'mirror.py'
            mirror_script.write_text('import time\ntime.sleep('
                                     + ('0.15' if mirror_first else '4') + ')\n')
            args = argparse.Namespace(
                card_root=root, headless=True, input=None, snapshot=None,
                run_for_ms=0, no_mirror=False, mirror='kitty',
                run_log=None, quiet=False, log=None)
            processes = []
            real_popen = subprocess.Popen

            def launch(*a, **kw):
                process = real_popen(*a, **kw)
                processes.append(process)
                return process

            def cleanup(command, **kw):
                self.assertEqual(command, ['tmux', 'kill-session', '-t',
                                           '7xtgv-desktop-' + str(processes[0].pid)])
                if processes[1].poll() is None:
                    processes[1].terminate()

            try:
                with patch.object(desktop, 'mirror_command', return_value=(
                        sys.executable, [sys.executable, str(mirror_script)], os.environ.copy())), \
                        patch.object(desktop.subprocess, 'Popen', side_effect=launch), \
                        patch.object(desktop.subprocess, 'run', side_effect=cleanup) as clean:
                    result = desktop.run(args, 'jc8012p4a1c', root)
                self.assertEqual(result, 0)
                self.assertEqual((root / 'saved').read_text(), 'saved')
                self.assertEqual(len(processes), 2)
                self.assertTrue(all(p.poll() is not None for p in processes))
                clean.assert_called_once()
            finally:
                for p in processes:
                    if p.poll() is None:
                        p.kill()
                    p.wait()
                    if p.stdout:
                        p.stdout.close()

    def test_mirror_close_cooperatively_stops_quiet_guest(self):
        self.check_close(mirror_first=True)

    def test_guest_close_cleans_up_mirror(self):
        self.check_close(mirror_first=False)

    def test_mirror_close_wins_over_reboot(self):
        self.check_close(mirror_first=True, reboot=True)


if __name__ == '__main__':
    unittest.main()
