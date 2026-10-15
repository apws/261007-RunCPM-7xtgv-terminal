"""Per-launch Mintty settings; never modify the user's global configuration.

KeyFunctions contains literal ESC bytes, with US as its alternate separator.
See https://mintty.github.io/mintty.1.html (KeyFunctions).
Printable characters retain the active Windows layout, including AltGr.
"""
from pathlib import Path
import hashlib
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from key_event_ids import key_ids


def bindings():
    ids = key_ids()
    result = {}

    def event(name, base, mods, fallback):
        prefix = ''.join(c for bit, c in ((1, 'C'), (2, 'S'), (4, 'A')) if mods & bit)
        key = (prefix + '+' if prefix else '') + name
        encoded = ''.join(f'{b:03d}' for b in fallback.encode('utf-8'))
        result[key] = f'"\x1b[>70;1;{ids[base]};{mods};{encoded}v"'

    nav = [('Left', 'Left', 'D', None), ('Right', 'Right', 'C', None),
           ('Up', 'Up', 'A', None), ('Down', 'Down', 'B', None),
           ('Home', 'Home', 'H', None), ('End', 'End', 'F', None),
           ('Insert', 'Insert', None, 2), ('Delete', 'Delete', None, 3),
           ('Prior', 'PageUp', None, 5), ('Next', 'PageDown', None, 6)]
    for name, base, final, number in nav:
        for mods in range(8):
            xm = 1 + bool(mods & 2) + 2 * bool(mods & 4) + 4 * bool(mods & 1)
            fallback = (f'\x1b[1;{xm}{final}' if final else f'\x1b[{number};{xm}~')
            if not mods:
                fallback = f'\x1b[{final}' if final else f'\x1b[{number}~'
            for key in (name, 'KP_' + name):
                event(key, 'Key' + base, mods, fallback)
    for index, code in enumerate(('P', 'Q', 'R', 'S', 15, 17, 18, 19, 20, 21, 23, 24), 1):
        for mods in range(8):
            xm = 1 + bool(mods & 2) + 2 * bool(mods & 4) + 4 * bool(mods & 1)
            if mods:
                fallback = f'\x1b[1;{xm}{code}' if isinstance(code, str) else f'\x1b[{code};{xm}~'
            else:
                fallback = f'\x1bO{code}' if isinstance(code, str) else f'\x1b[{code}~'
            event(f'F{index}', f'KeyF{index}', mods, fallback)
    for name, base, byte in [('Back', 'Backspace', '\x08'), ('Esc', 'Escape', '\x1b'),
                             ('Enter', 'Enter', '\r'), ('KP_Enter', 'Enter', '\r'),
                             ('Space', 'Space', ' ')]:
        for mods in range(8):
            if not mods and name in ('Enter', 'KP_Enter', 'Space'):
                continue  # Mintty only permits modified forms of these keys.
            fallback = '\0' if base == 'Space' and mods & 1 else byte
            fallback = ('\x1b' if mods & 4 else '') + fallback
            event(name, 'Key' + base, mods, fallback)
    overrides = {'C+Back': '\x18', 'CS+Back': '\x1b[>65v',
                 'A+Space': '\x1b[>14v', 'C+Space': '\x1b[>14v',
                 'A+Back': '\x1b[>61v', 'SA+Back': '\x1b[>4v'}
    for kp in ('', 'KP_'):
        # Bank selection is a global control, not an application-profile
        # Alt+Page key event. Match the established Konsole/Kitty routes.
        overrides.update({f'A+{kp}Prior': '\x1b[>12v', f'A+{kp}Next': '\x1b[>13v',
                          f'SA+{kp}Home': '\x1b[1;3H', f'SA+{kp}End': '\x1b[1;3F',
                          f'CS+{kp}Prior': '\x1b[>67v', f'CS+{kp}Next': '\x1b[>66v',
                          f'CSA+{kp}Prior': '\x1b[>63;33v', f'CSA+{kp}Next': '\x1b[>63;34v'})
    # Windows NumLock+Shift yields keypad navigation keys and may remove
    # Shift before Mintty's KeyFunctions lookup (confirmed by native probe).
    # Treat these physical keypad positions as numeric shortcut aliases.
    # Dedicated navigation keys keep their existing mappings.
    keypad_digits = ('Insert', 'End', 'Down', 'Next', 'Left',
                     'Begin', 'Right', 'Home', 'Up', 'Prior')
    levels = (0, 4, 7, 11, 14, 18, 21, 25, 28, 31)
    for digit, name in enumerate(keypad_digits):
        if digit:
            for mods in ('C', 'CS'):
                overrides[f'{mods}+KP_{name}'] = f'\x1b[>63;{levels[digit]}v'
        if digit <= 2:
            control = '\x1b[>63;0v' if digit == 0 else f'\x1b[>{digit}v'
            for mods in ('CA', 'CSA'):
                overrides[f'{mods}+KP_{name}'] = control
    result.update({key: f'"{value}"' for key, value in overrides.items()})
    # Numeric actions keep the ACTUAL modifiers. Literal character actions
    # cannot safely distinguish Alt+0 (bank) from Ctrl+Alt+Shift+0 (off).
    # Mintty uses unshifted layout names; bare character tags imply Ctrl+Shift.
    # Normal text is never rebound. The Windows-only decoder handles 900..912.
    for digit, czech in zip('1234567890', '+ěščřžýáíé'):
        for key in (digit, czech):
            result[key] = str(900 + int(digit))
            result['A+' + key] = str(900 + int(digit))
    result['A+q'] = '910'
    result['x'] = '911'
    result['CA+c'] = '912'
    result['F11'] = 'fullscreen'
    result['A+F4'] = 'close'
    # Pause is a layout-independent alternative for the existing Ctrl+] prefix.
    result['Pause'] = '^]'
    return result


def window_class(path):
    return 'xtgv-keys-' + hashlib.sha256(str(Path(path).resolve()).encode()).hexdigest()[:24]


def write_profile(path):
    settings = ['# Generated per session by tools-win/mintty_profile.py',
                'Class=' + window_class(path),
                'Columns=80', 'Rows=25', 'Font=Consolas', 'FontHeight=10',
                'Locale=C', 'Charset=UTF-8', 'Term=xterm-256color',
                'BackspaceSendsBS=yes', 'DeleteSendsDEL=no',
                'CtrlAltIsAltGr=no', 'AltGrIsAlsoAlt=no', 'ShootFoot=yes',
                'ClipShortcuts=no', 'WindowShortcuts=no', 'SwitchShortcuts=no',
                'ZoomShortcuts=no', 'AltFnShortcuts=no', 'CtrlShiftShortcuts=no']
    separator = '\x1f'
    settings.append('KeyFunctions=' + separator + separator.join(
        key + ':' + value for key, value in bindings().items()) + separator)
    Path(path).write_bytes(('\n'.join(settings) + '\n').encode('utf-8'))
