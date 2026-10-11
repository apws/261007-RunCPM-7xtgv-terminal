"""Opt-in SDL/PTY catalog browsing on a disposable snapshot of the working card.

Usage: python3 catalog_browsing.py EXECUTABLE EVIDENCE_DIRECTORY
Keeps mirror transcripts and GFX framebuffer snapshots; never runs guest apps.
"""
import argparse
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
from card_paths import card_root
os.environ['XTGV_DESKTOP_TRANSPORT'] = 'pty'
spec = importlib.util.spec_from_file_location('smoke', SKETCH/'tests/hardware_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def scenario(executable, evidence, autoexec):
    work = Path(tempfile.mkdtemp(prefix='autoexec-' if autoexec else 'browse-', dir=evidence))
    card = work/'card'
    for drive in ('A', 'B', 'C'):
        shutil.copytree(card_root()/drive, card/drive,
                        ignore=shutil.ignore_patterns('7XTGVTRM.INI', '7XTGVTRM.BUF', '$$$.SUB'))
    (card/'A/0/7XTGVTRM.INI').write_text(
        '[global]\ncrt_rampup_ms=0\nmode_palette=0\n'
        '[display:EP4_10]\nview_zoom=2\nzoom2_terminal=80x24\n')
    if autoexec:
        (card/'AUTOEXEC.TXT').write_bytes(b'7XF\r\n')
    proc = subprocess.Popen([str(executable.resolve()), '--card-root', str(card),
        '--pty', '--snapshot', str(work/'panel.ppm'), '--run-for-ms', '90000'],
        env=dict(os.environ, SDL_VIDEODRIVER='dummy'), stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True)
    fd = None
    raw = bytearray()
    transcript = []
    screen = smoke.Mirror(24)
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

        def prompt(expected):
            until = time.monotonic() + 6
            while time.monotonic() < until:
                pump(.05)
                if screen.text().rstrip().splitlines()[-1].strip() == expected:
                    return
            raise AssertionError((expected, screen.text(), work))

        active = '/a/cpm>' if autoexec else 'A0>'
        prompt(active)
        os.write(fd, b'\x1b[>29v')  # reserved automation profile 9, own INI
        pump(1.3)

        def command(text, expected, contains=(), excludes=()):
            nonlocal active
            os.write(fd, b'CLS\r')
            pump(.15)
            prompt(active)
            os.write(fd, text.encode('ascii') + b'\r')
            pump(.2)
            prompt(expected)
            active = expected
            result = screen.text()
            for item in contains:
                assert item in result, (text, item, result)
            for item in excludes:
                assert item not in result, (text, item, result)
            assert 'Unknown area' not in result and 'Usage:' not in result, result
            transcript.append(text + '\n' + result + '\n')
            print('PASS:', text, '->', expected, flush=True)
            return result

        if not autoexec:
            # Catalog commands work before selecting named prompt presentation.
            command('LL /', 'A0>', ('/a ', '/b ', '/c '))
            command('7XF', '/a/cpm>')
            command('PAGE 0', active)
            command('LS /', active, ('/a ', '/b ', '/c '))
            for drive in ('A', 'B', 'C'):
                for verb in ('LS', 'LL'):
                    result = command(f'{verb} /{drive}', active)
                    description = next(line[2:] for line in
                        (card/'A/0/VOL.7XF').read_text().splitlines()
                        if line[:2].upper() == drive + ' ')
                    assert (' ' + description)[:79] in result, result
            areas = ('A/0', 'A/1', 'B/1', 'B/2', 'B/3', 'B/4', 'B/5',
                     'B/6', 'C/1', 'C/2', 'C/3')
            for area in areas:
                alias = (card/area/'DIR.7XF').read_text().split()[0]
                command(f'CD /{area[0]}/{alias}', f'/{area[0].lower()}/{alias}>')
                command('CD', active, (f'{area[0]}{int(area[2], 16)}:',))
            command('CD /B/MBASIC', '/b/mbasic>')
            command('LS MBASIC.*', active, ('mbasic.com',), ('Microsoft BASIC working',))
            command('LL MBASIC.*', active, ('mbasic.com', 'Microsoft BASIC working'))
            command('DIR MBASIC.*', active, ('MBASIC', 'COM'), ('Microsoft BASIC working',))
            command('LDIR MBASIC.*', active, ('MBASIC.COM',), ('Microsoft BASIC working',))
            command('CD /A/CPM', '/a/cpm>')
            command('CD /B', '/b/mbasic>')  # remembered drive selection
            # This is a working-card snapshot, not a fixed catalog-only fixture.
            # A maintainer's ordinary placeholder file makes an area nonempty.
            comal_has_content = any(p.is_file() and not p.name.startswith('.') and
                                    p.name.upper() != 'DIR.7XF'
                                    for p in (card/'B/3').iterdir())
            result = command('LL ..', active, ('/b/mbasic',))
            assert ('/b/comal' in result) == comal_has_content, result
        else:
            command('CD', active, ('A0:', '/a/cpm'))
        pump(.3)
        (work/'mirror.txt').write_text(screen.text() + '\n')
        proc.terminate()
        stdout, stderr = proc.communicate(timeout=5)
        assert proc.returncode == 0, stderr
        assert (work/'panel.ppm').stat().st_size > 1000
        (work/'run.log').write_text(stdout + stderr)
        print('Evidence:', work, flush=True)
    finally:
        (work/'transcript.txt').write_text('\n'.join(transcript))
        (work/'mirror.bin').write_bytes(raw)
        if fd is not None:
            smoke.bridge.close_serial(fd, saved)
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('evidence', type=Path)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    scenario(args.executable, args.evidence, False)
    scenario(args.executable, args.evidence, True)
