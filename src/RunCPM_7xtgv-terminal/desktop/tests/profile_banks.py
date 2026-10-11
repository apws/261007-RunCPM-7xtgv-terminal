"""Real TE cursor/Backspace after profile renumbering, on disposable SDL cards."""
import importlib.util
import os
from pathlib import Path
import re
import select
import shutil
import subprocess
import sys
import tempfile
import time

SKETCH = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SKETCH / 'tools'))
from card_paths import card_root, require_files
os.environ['XTGV_DESKTOP_TRANSPORT'] = 'pty'
spec = importlib.util.spec_from_file_location('smoke', SKETCH / 'tests/hardware_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def scenario(executable, evidence, slot):
    work = evidence / f'te-bank-{slot}'
    area = work / 'card/A/0'
    area.mkdir(parents=True)
    source = card_root() / 'A/0'
    require_files(source / 'TE.COM', source / '7XTGVTRM.INI')
    shutil.copyfile(source / 'TE.COM', area / 'TE.COM')
    shift = (slot - 7) % 10
    ini = (source / '7XTGVTRM.INI').read_bytes().decode()
    ini = re.sub(r'\[bank([0-9])\]', lambda m: f'[bank{(int(m[1]) + shift) % 10}]', ini)
    ini = ini.replace('ccp_bank=0', f'ccp_bank={shift}')
    ini = ini.replace('crt_rampup_ms=1000', 'crt_rampup_ms=0')
    ini = ini.replace('zoom2_terminal=102x40', 'zoom2_terminal=80x24')
    (area / '7XTGVTRM.INI').write_bytes(ini.encode())
    (area / 'VTEDIT.TXT').write_bytes(b'ABCDEF\r\nSECOND ROW\r\n\x1a')
    proc = subprocess.Popen([executable, '--card-root', str(area.parents[1]), '--pty',
        '--snapshot', str(work / 'panel.ppm'), '--run-for-ms', '20000'],
        env=dict(os.environ, SDL_VIDEODRIVER='dummy'), stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True)
    fd = None
    screen = smoke.Mirror(24)
    raw = bytearray()
    try:
        assert proc.stdout.readline().startswith('Desktop ')
        endpoint = proc.stdout.readline().strip().removeprefix('PTY=')
        fd, saved = smoke.bridge.open_serial(endpoint)

        def pump(seconds):
            until = time.monotonic() + seconds
            while time.monotonic() < until:
                if select.select([fd], [], [], .02)[0]:
                    data = os.read(fd, 32768)
                    raw.extend(data)
                    screen.feed(data)

        def expect(text):
            until = time.monotonic() + 5
            while text not in screen.text() and time.monotonic() < until:
                pump(.05)
            assert text in screen.text(), (slot, text, screen.text())

        expect('A0>')
        os.write(fd, f'\x1b[>{20 + slot}v'.encode())
        pump(1.3)
        os.write(fd, b'TE VTEDIT.TXT\r')
        expect('ABCDEF')
        # Two Right events, then physical Backspace: remove B from ABCDEF.
        os.write(fd, b'\x1b[>70;1;2;0;v\x1b[>70;1;2;0;v\x1b[>70;1;11;0;v')
        expect('ACDEF')
        assert 'ABCDEF' not in screen.text(), screen.text()
        print(f'PASS: real TE Right/Right/Backspace in bank {slot}, CCP bank {shift}')
    finally:
        if fd is not None:
            smoke.bridge.termios.tcsetattr(fd, smoke.bridge.termios.TCSANOW, saved)
            os.close(fd)
        proc.terminate()
        stdout, stderr = proc.communicate(timeout=5)
        (work / 'run.log').write_text(stdout + stderr)
        (work / 'mirror.txt').write_text(screen.text())
        (work / 'pty.bin').write_bytes(raw)


def run(executable, evidence):
    evidence.mkdir(parents=True, exist_ok=True)
    for slot in (7, 1, 0):
        scenario(executable, evidence, slot)


if __name__ == '__main__':
    run(str(Path(sys.argv[1]).resolve()), Path(sys.argv[2]) if len(sys.argv) > 2 else
        Path(tempfile.mkdtemp(prefix='xtgv-profile-banks-')))
