"""Filtered history submission and CRC persistence through real SDL/PTY + MBASIC.

Usage: python3 history_recall.py EXECUTABLE
Only a disposable card and automation profile 9 are used.
"""
import importlib.util
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time

SKETCH = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SKETCH / 'tools'))
from card_paths import card_root, require_files
from history_to_tokens import entries

os.environ['XTGV_DESKTOP_TRANSPORT'] = 'pty'
spec = importlib.util.spec_from_file_location('smoke', SKETCH/'tests/hardware_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def run(executable):
    source = card_root()/'C/1/MBASIC.COM'
    require_files(source)
    with tempfile.TemporaryDirectory(prefix='xtgv-history-recall-') as directory:
        card = Path(directory)
        area = card/'A/0'
        area.mkdir(parents=True)
        shutil.copyfile(source, area/'MBASIC.COM')
        (area/'7XTGVTRM.INI').write_text(
            '[global]\nccp_bank=0\ncrt_rampup_ms=0\n[display:EP4_10]\n'
            'view_zoom=2\nzoom2_terminal=80x24\n'
            '[bank9]\nname=AUTOMATION\non_select=\n')
        (area/'7XTGVTRM.BUF').write_bytes(
            b'# 7xtgv terminal history v1\n[9]\n>PRINT 11\n>PRINT 22\n>LIST\n')
        for reboot in (False, True):
            proc = subprocess.Popen([executable, '--card-root', str(card), '--pty',
                '--run-for-ms', '20000'], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, env=dict(os.environ, SDL_VIDEODRIVER='dummy'))
            fd = None
            screen = smoke.Mirror(80)
            try:
                assert proc.stdout.readline().startswith('Desktop ')
                endpoint = proc.stdout.readline().strip().removeprefix('PTY=')
                fd, saved = smoke.bridge.open_serial(endpoint)

                def pump(seconds):
                    until = time.monotonic() + seconds
                    while time.monotonic() < until:
                        if select.select([fd], [], [], .02)[0]:
                            screen.feed(os.read(fd, 32768))

                def expect(text):
                    until = time.monotonic() + 5
                    while text not in screen.text() and time.monotonic() < until:
                        pump(.05)
                    assert text in screen.text(), (text, screen.text())

                expect('A0>')
                os.write(fd, b'\x1b[>29v')
                pump(1.3)  # profile label consumes input until it has closed
                os.write(fd, b'MBASIC\r')
                expect('OK')
                pump(.15)
                if not reboot:
                    os.write(fd, b'PR\x1b[>10v')
                    expect('PRINT 22')
                    os.write(fd, b'\r')
                    pump(.2)
                    os.write(fd, b'10 LI\x1b[>10v')
                    expect('10 LIST')
                    os.write(fd, b'\r')
                    pump(.2)
                else:
                    # An unsubmitted recall proves the new line loaded after restart,
                    # but must not append another entry on shutdown.
                    os.write(fd, b'10\x1b[>10v')
                    expect('10 LIST')
                    pump(.1)
                proc.terminate()
                _, stderr = proc.communicate(timeout=5)
                assert proc.returncode == 0, stderr
                data = (area/'7XTGVTRM.BUF').read_bytes()
                assert data.startswith(b'# 7xtgv terminal history v2\n')
                assert entries(data, 9) == [b'PRINT 11', b'PRINT 22', b'LIST',
                                             b'PRINT 22', b'10 LIST'], entries(data, 9)
                for bank in range(1, 9):
                    assert entries(data, bank) == [], bank
            finally:
                if fd is not None:
                    smoke.bridge.close_serial(fd, saved)
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=5)
    print('Filtered recall, prefixed completion, CRC save/reload, cancelled recall: PASS')


if __name__ == '__main__':
    run(str(Path(sys.argv[1]).resolve()))
