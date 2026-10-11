"""Integrated 7XF listings at 40/80/100 columns, on disposable cards only."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def guest_text(path, text):
    path.write_bytes(text.replace('\n', '\r\n').encode('ascii') + b'\x1a')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    for columns in (40, 80, 100):
        with tempfile.TemporaryDirectory(prefix='xtgv-catalog-listing-') as temp:
            card = Path(temp)
            for area in ('A/0', 'C/1', 'C/2'):
                (card / area).mkdir(parents=True)
            guest_text(card/'A/0/7XTGVTRM.INI',
                '[global]\ncrt_rampup_ms=0\n[display:EP4_10]\n'
                f'view_zoom=2\nzoom2_terminal={columns}x24\n')
            detail = 'VISIBLE-' + 'x' * 65
            guest_text(card/'A/0/VOL.7XF', 'a System\nc DRIVE-' + detail + '\n')
            guest_text(card/'C/1/DIR.7XF', 'sources      ' + detail + '\n' +
                'VTAA.COM     ' + detail + '\n')
            guest_text(card/'C/2/DIR.7XF', 'demos        Graphics demos\n')
            (card/'C/2/.gitkeep').write_bytes(b'')
            # Deliberately neither filename nor extension order on disk.
            names = ('VTAA.Z80', 'VTBB.COM', 'VTZZ.ASM', 'VTAA.COM', 'VTNONE')
            for name in names:
                (card/'C/1'/name).write_bytes(b'not executed')
            keyboard = card/'input.bin'
            keyboard.write_bytes(b'PAGE 0\rC1:\r'
                b'LS VT* -O SHORT.7XF\rLL VT* -O LONG.7XF\r'
                b'LS .. -O PARENT.7XF\rLL .. -O DETAIL.7XF\r'
                b'LL / -O ROOT.7XF\rEXIT\r')
            result = subprocess.run([str(args.executable.resolve()), '--headless',
                '--card-root', str(card), '--input', str(keyboard),
                '--run-for-ms', '4000'], capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            for filename in names:
                assert (card/'C/1'/filename).read_bytes() == b'not executed'
                assert not (card/'C/1'/filename.lower()).exists()
            captured = {name: (card/'C/1'/f'{name}.7XF').read_text()
                        for name in ('SHORT', 'LONG', 'PARENT', 'DETAIL', 'ROOT')}
            expected = ('vtnone', 'vtzz.asm', 'vtaa.com', 'vtbb.com', 'vtaa.z80')
            for name in ('SHORT', 'LONG'):
                content = captured[name]
                positions = [content.index(filename) for filename in expected]
                assert positions == sorted(positions), content
                heading = next(line for line in content.splitlines() if line.startswith(' VISIBLE-'))
                assert '/c/sources' not in content, content
            long_row = next(line for line in captured['LONG'].splitlines() if 'vtaa.com' in line)
            assert 'vtaa.com      ' in long_row, long_row
            assert len(long_row) == columns - 1, long_row
            assert 'VISIBLE-' not in next(line for line in captured['SHORT'].splitlines()
                                          if 'vtaa.com' in line)
            assert '/c/sources' in captured['PARENT'] and '/c/demos' not in captured['PARENT']
            short_area = next(line for line in captured['PARENT'].splitlines()
                              if '/c/sources' in line)
            assert short_area.endswith('/c/sources'), short_area
            assert '/c/demos' not in captured['DETAIL']
            for name in ('PARENT', 'DETAIL'):
                heading = captured[name].splitlines()[1]
                assert heading.startswith(' DRIVE-VISIBLE-'), heading
                assert len(heading) <= columns - 1, heading
            assert '/a' not in captured['PARENT']
            area_row = next(line for line in captured['DETAIL'].splitlines() if '/c/sources' in line)
            assert '/c/sources    V' in area_row, area_row
            if columns >= 80:
                assert '/c/sources    VISIBLE-' in area_row, area_row
            assert '/a' in captured['ROOT'] and '/c ' in captured['ROOT']
            for content in captured.values():
                assert '\t' not in content
                assert all(len(line) < columns for line in content.splitlines())
            print(f'PASS: integrated EP4_10 7XF listing at {columns} columns')


if __name__ == '__main__':
    main()
