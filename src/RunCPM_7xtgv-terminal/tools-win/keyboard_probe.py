"""Capture a few physical Mintty shortcuts without launching an emulator/card."""
import argparse
import json
import os
from pathlib import Path
import select
import subprocess
import tempfile
import termios
import time
import tty

from windows_build import OUTPUT, require_msys, target_name
from mintty_profile import write_profile
from mintty_keys import MinttyInput, BRIGHTNESS
from terminal import Keys
from key_bridge import KeyBridge, mintty_environment
from key_event_ids import key_ids

CONTROL_CASES = (
    ('Alt+Q (application exit)', b'\x1b[>60v'),
    ('Alt+0 (top row: bank, must not turn backlight off)', b'\x1b[>20v'),
    ('Alt+# (CZ: Alt+AltGr+X; US: Alt+Shift+3; RAW toggle)', b'\x1b[>15v'),
    ('Ctrl+Shift+X (transfer panel)', b'\x1dm'),
    ('Alt+PgUp', b'\x1b[>12v'), ('Alt+PgDn', b'\x1b[>13v'),
    ('Ctrl+Shift+PgUp', b'\x1b[>67v'), ('Ctrl+Shift+PgDn', b'\x1b[>66v'),
    ('Ctrl+Alt+Shift+PgUp', b'\x1b[>63;33v'),
    ('Ctrl+Alt+Shift+PgDn', b'\x1b[>63;34v'),
)


def cases(section):
    if section in ('all', 'controls'):
        yield from CONTROL_CASES
    if section in ('all', 'typing'):
        ids = key_ids()
        configured = os.environ.get('XTGV_HACK_TOPROW', '+ěščřžýáíé')
        for digit, char in zip('1234567890', configured or '+ěščřžýáíé'):
            fallback = ''.join(f'{b:03d}' for b in char.encode())
            expected = f'\x1b[>70;1;{ids["KeyNum" + digit]};0;{fallback}v'.encode() if configured else char.encode()
            yield (f'{char} (TOP ROW {digit}, no modifiers, CapsLock OFF; conversion {"ON" if configured else "OFF/CZ fixture"})', expected)
        yield ('NUMPAD decimal comma (NumLock ON, no modifiers)',
               f'\x1b[>70;1;{ids["KeyKpDecimal"]};0;044v'.encode())
        yield ('ordinary comma (main keyboard)', b',')
    for location in ('top-row', 'numpad'):
        if section not in ('all', location):
            continue
        label = 'TOP ROW' if location == 'top-row' else 'NUMPAD, NumLock ON'
        for digit, name, control in ((0, 'backlight OFF', b'\x1b[>63;0v'),
                                     (1, 'ANSI', b'\x1b[>1v'),
                                     (2, 'VT52', b'\x1b[>2v')):
            yield (f'Ctrl+Alt+Shift+{digit} ({label}; {name})', control)
        for digit in range(1, 10):
            yield (f'Ctrl+Shift+{digit} ({label}; brightness)',
                   f'\x1b[>63;{BRIGHTNESS[digit]}v'.encode())


def translate_capture(wire):
    # Exercise both stages used by the real mirror, including Alt+# fallback.
    keys = Keys()
    actual = keys.feed(MinttyInput().feed(wire + b'\r')).removesuffix(b'\r')
    return b'\x1dm' if keys.xmodem else actual


def capture(directory, section):
    saved = termios.tcgetattr(0)
    results = []
    def show(message):
        os.write(1, (message + '\r\n').encode())
    try:
        tty.setraw(0)
        show('Keyboard probe: checks key translation only; no emulator/card or backlight changes.')
        show('Alt means LAlt (not AltGr).')
        show('For each step: press the chord once, release it, then press Enter.')
        show('Use the named key location. For NUMPAD tests, turn NumLock ON.')
        show('Typing checks physical-key packets when enabled; firmware applies digit/decimal defaults afterwards.')
        show('Enter alone skips a step. Escape alone ends the probe.')
        for chord, expected in cases(section):
            show('\r\nPress ' + chord + ', then Enter:')
            received = bytearray()
            while True:
                data = os.read(0, 256)
                if not data:
                    return 1
                received.extend(data)
                if b'\r' in received or b'\n' in received:
                    break
                # Distinguish a standalone Escape from an escape sequence.
                if received == b'\x1b' and not select.select([0], [], [], .15)[0]:
                    return 1
            wire = bytes(received).rstrip(b'\r\n')
            actual = translate_capture(wire)
            ok = actual == expected
            results.append(dict(chord=chord, expected=expected.hex(), received=actual.hex(), wire=wire.hex(), passed=ok, skipped=not wire))
            (directory / 'keys.json').write_text(json.dumps(results, indent=2) + '\n')
            show(('SKIPPED' if not wire else 'PASS' if ok else 'DIFFERENT') +
                 f'  translated {actual!r}; expected {expected!r}')
            if wire and not ok:
                show(f'  Mintty sent {wire!r}')
        show('\r\nComplete. Results saved. Press Enter to close.')
        os.read(0, 256)
    finally:
        termios.tcsetattr(0, termios.TCSANOW, saved)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--section', choices=('all', 'controls', 'top-row', 'numpad', 'typing'), default='all')
    parser.add_argument('--target', type=target_name, default=target_name('ES3_5'))
    args = parser.parse_args()
    if args.capture:
        build = OUTPUT / 'msys2-ucrt64' / args.target
        with KeyBridge(build, args.capture / 'mintty.conf'):
            return capture(args.capture, args.section)
    require_msys()
    parent = OUTPUT / 'keyboard-proofs'
    parent.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix=time.strftime('%y%m%d-%H%M-'), dir=parent))
    profile = directory / 'mintty.conf'
    write_profile(profile)
    print('Keyboard evidence:', directory, flush=True)
    with (directory / 'mintty.log').open('wb') as log:
        return subprocess.call(['/usr/bin/mintty.exe', '--config', str(profile), '--hold', 'error',
                                '--title', '7xtgv-terminal keyboard probe', '--exec',
                                '/usr/bin/python3', str(Path(__file__).resolve()),
                                '--capture', str(directory), '--section', args.section, '--target', args.target],
                               env=mintty_environment(), stdout=log, stderr=subprocess.STDOUT)


if __name__ == '__main__':
    raise SystemExit(main())
