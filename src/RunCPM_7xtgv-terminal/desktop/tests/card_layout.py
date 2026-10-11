"""Exercise current card aliases and CCP command lookup on a disposable copy.

Usage: python3 desktop/tests/card_layout.py /path/to/xtgv_desktop
No real card, live mirror, firmware build or user INI changes.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

SKETCH = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SKETCH / 'tools'))
from card_paths import card_root, require_files


def probe(path, marker):
    path.parent.mkdir(parents=True, exist_ok=True)
    # BDOS 4 punches one marker to host PUN.TXT, then returns to CCP.
    path.write_bytes(bytes((0x1e, ord(marker), 0x0e, 4, 0xcd, 5, 0, 0xc9)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='xtgv-card-layout-') as temp:
        root = Path(temp)
        card = root / 'card'
        # Copy catalogs only: real application files cannot mask probe failures.
        aliases = {'A/0': 'cpm', 'A/1': 'cpmdev', 'B/1': 'mbasic',
                   'B/2': 'bbcbasic', 'B/3': 'comal', 'B/4': 'tp',
                   'C/1': '7xtgv', 'C/2': 'xterm256', 'C/3': 'bench'}
        commands = [b'7XF']
        expected = ''
        for index, (area, alias) in enumerate(aliases.items()):
            source = card_root() / area / 'DIR.7XF'
            require_files(source)
            assert source.read_bytes().split()[0].decode() == alias
            target = card / area
            target.mkdir(parents=True)
            shutil.copyfile(source, target / 'DIR.7XF')
            marker = str(index)
            probe(target / 'VTALIAS.COM', marker)
            commands += [f'CD /{area[0]}/{alias}'.encode(), b'VTALIAS']
            expected += marker
        # B0/C0 are absent, as in a fresh clone; ordinary directories suffice.
        assert not (card/'B/0').exists() and not (card/'C/0').exists()
        probe(card/'A/0/VTLOOK.COM', 'A')
        probe(card/'B/1/VTLOOK.COM', '1')
        probe(card/'D/5/VTLOOK.COM', 'D')  # no FORMAT, no D0, no catalog
        commands += [b'B1:', b'VTLOOK', b'B2:', b'VTLOOK', b'D5:', b'VTLOOK']
        expected += '1AD'
        # Explicit drive suppresses fallback; B3 has no VTLOOK.
        commands += [b'C3:', b'B:VTLOOK', b'VTLOOK', b'EXIT']
        expected += 'A'
        keyboard = root/'input.bin'
        keyboard.write_bytes(b'\r'.join(commands)+b'\r')
        result = subprocess.run([str(args.executable.resolve()), '--headless',
            '--card-root', str(card), '--input', str(keyboard),
            '--run-for-ms', '8000'], capture_output=True, timeout=12)
        assert result.returncode == 0, result.stderr
        punch = card/'A/0/PUN.TXT'
        assert punch.read_bytes() == expected.encode(), (
            expected, punch.read_bytes(), result.stderr)
        # Now prove the final current-drive/user-zero fallback separately.
        punch.unlink()  # Fresh output in this disposable fixture, not the card.
        probe(card/'B/0/VTZERO.COM', 'b')
        probe(card/'C/0/VTZERO.COM', 'c')
        keyboard.write_bytes(b'B2:\rVTZERO\rC3:\rVTZERO\rEXIT\r')
        result = subprocess.run([str(args.executable.resolve()), '--headless',
            '--card-root', str(card), '--input', str(keyboard),
            '--run-for-ms', '8000'], capture_output=True, timeout=12)
        assert result.returncode == 0, result.stderr
        assert punch.read_bytes() == b'bc', punch.read_bytes()
    print('PASS: nine named areas; COM current DU -> A0 -> drive/user 0;')
    print('      explicit-drive isolation; plain D5 directory without FORMAT/D0')


if __name__ == '__main__':
    main()
