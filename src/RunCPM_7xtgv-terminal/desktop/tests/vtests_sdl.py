"""Opt-in real MBASIC/VTESTS through the integrated SDL firmware, no hardware.

Each page runs on a disposable card containing only canonical MBASIC/VTESTS.
SDL's dummy driver is quiet but still exercises the presenter. Retain raw PTY
output, reconstructed mirror and actual GFX framebuffer PPMs for inspection.
Usage: python3 vtests_sdl.py /path/to/xtgv_desktop /path/to/evidence
"""
import argparse
import importlib.util
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time
import sys

SKETCH = Path(__file__).resolve().parents[2]
REPO = SKETCH.parents[1]
sys.path.insert(0, str(SKETCH / 'tools'))
from card_paths import card_root, require_files
os.environ['XTGV_DESKTOP_TRANSPORT'] = 'pty'
spec = importlib.util.spec_from_file_location('smoke', SKETCH / 'tests/hardware_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def scenario(executable, evidence, choice, stage=1):
    work = Path(tempfile.mkdtemp(prefix=f'page-{choice}-{stage}-', dir=evidence))
    area = work / 'card/A/0'
    area.mkdir(parents=True)
    source = card_root() / 'A/7'
    require_files(source / 'MBASIC.COM', source / 'VTESTS.BAS')
    for name in ('MBASIC.COM', 'VTESTS.BAS'):
        shutil.copyfile(source / name, area / name)
    # Own isolated INI, never modify the user's real/local card or active bank.
    # Explicit 80x24 logical geometry is required by VTESTS page 9.
    (area / '7XTGVTRM.INI').write_text(
        '[global]\ncrt_rampup_ms=0\nmode_palette=0\n'
        '[display:EP4_10]\nview_zoom=2\nzoom2_terminal=80x24\n'
        'render_flush_hz=10\n'
        '[display:EC6_147]\nview_zoom=1\nzoom1_terminal=80x24\n')
    process = subprocess.Popen([str(executable), '--card-root', str(area.parents[1]),
        '--pty', '--snapshot', str(work / 'panel.ppm'), '--run-for-ms', '20000'],
        env=dict(os.environ, SDL_VIDEODRIVER='dummy'), stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True)
    fd = None
    screen = smoke.Mirror(24)
    raw = bytearray()
    try:
        assert process.stdout.readline().startswith('Desktop ')
        endpoint = process.stdout.readline().strip().removeprefix('PTY=')
        fd, saved = smoke.bridge.open_serial(endpoint)

        def pump(seconds):
            until = time.monotonic() + seconds
            while time.monotonic() < until:
                if select.select([fd], [], [], .02)[0]:
                    try:
                        data = os.read(fd, 16384)
                    except OSError:
                        if process.poll() is not None:
                            return
                        raise
                    raw.extend(data)
                    screen.feed(data)

        def expect(needle):
            until = time.monotonic() + 6
            while needle not in screen.text() and time.monotonic() < until:
                pump(.05)
            pump(.2)
            assert needle in screen.text(), (needle, screen.text(), work)
            assert ' error' not in screen.text().lower(), screen.text()

        expect('A0>')
        os.write(fd, b'\x1b[>29v')  # reserved automation profile 9
        pump(1.2)  # temporary bank label consumes input until it closes
        os.write(fd, b'MBASIC\r')
        expect('OK')
        os.write(fd, b'RUN "VTESTS"\r')
        expect('Select:')
        os.write(fd, f'{choice}\r'.encode())
        prompt = ('Enter: return to ANSI' if choice == 5 else
                  'Enter: next scroll check' if choice == 9 else
                  'Enter returns to menu:')
        expect(prompt)
        if choice == 9:
            for step in range(2, stage + 1):
                os.write(fd, b'\r')
                expect(f'{step}/5:')
        pump(.4)  # allow buffered 10 Hz presentation
        rows = [''.join(r).rstrip() for r in screen.rows]
        if choice == 5:
            assert rows[2] == 'ABCD moves: [PASS]', rows
            assert rows[4] == 'Erase K: KEEP', rows
            assert rows[6] == 'Erase J: KEEP' and not rows[7], rows
        if choice == 9:
            if stage < 5:
                assert rows[0] == 'HEADER stays at row 1', rows
                assert rows[22].startswith('FOOTER stays at row 23'), rows
            if stage == 1:
                assert rows[1].strip() == 'SCROLL 6' and rows[20].strip() == 'SCROLL 25', rows
                assert not rows[21], rows
            elif stage == 2:
                assert not rows[1] and rows[2].strip() == 'SCROLL 6', rows
            elif stage == 3:
                assert not rows[9] and rows[10].strip() == 'ROW 10', rows
            elif stage == 4:
                assert rows[4] == 'TOP CLAMP' and rows[19] == 'BOTTOM CLAMP', rows
            else:
                assert rows[0].strip() == 'ROW 2', rows
                assert rows[21].startswith('5/5: expect ROW 2'), rows
                assert rows[22] == 'FULL-SCREEN SCROLL', rows
        (work / 'mirror.txt').write_text(screen.text() + '\n')
        process.terminate()
        while process.poll() is None:
            pump(.05)
        stdout, stderr = process.communicate(timeout=2)
        (work / 'run.log').write_text(stdout + stderr)
        assert process.returncode == 0, (stdout, stderr)
        assert (work / 'panel.ppm').stat().st_size > 1000
        print(f'PASS {choice}/{stage}: {work}')
    finally:
        (work / 'mirror.bin').write_bytes(raw)
        if fd is not None:
            smoke.bridge.close_serial(fd, saved)
        if process.poll() is None:
            process.kill()
            process.wait()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--choice', type=int, choices=(3, 5, 7, 9),
                        help='Run just one menu test (9 captures all five stages)')
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    for choice, stage in [(3, 1), (5, 1), (7, 1)] + [(9, n) for n in range(1, 6)]:
        if args.choice is None or args.choice == choice:
            scenario(args.executable.resolve(), args.evidence.resolve(), choice, stage)
