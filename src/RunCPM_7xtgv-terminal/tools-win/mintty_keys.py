"""Decode Mintty numeric actions and modified digit reports before the mirror.

Unlike literal KeyFunctions strings, numeric actions retain the actual xterm
modifier value. Codes 900..912 are private to the generated Windows profile.
"""
import re
import time

BRIGHTNESS = (0, 4, 7, 11, 14, 18, 21, 25, 28, 31)

# Mintty bypasses character KeyFunctions for some modified keypad keys.
# Its app_pad_code/mod_ss3 paths emit CSI 1;modifier p..y for digits 0..9.
# Also accept explicit modified top-row digits (xterm and CSI-u formats).
# Only the configured brightness/mode chords are intercepted; unassigned
# keypad controls, ordinary digits, AltGr text and navigation stay byte-exact.
DIGIT_REPORTS = {}
for _digit in range(10):
    for _modifier in (6, 8):
        if _modifier == 6 and _digit:
            _control = f'\x1b[>63;{BRIGHTNESS[_digit]}v'.encode()
        elif _modifier == 8 and _digit == 0:
            _control = b'\x1b[>63;0v'
        elif _modifier == 8 and _digit in (1, 2):
            _control = f'\x1b[>{_digit}v'.encode()
        else:
            continue
        for _packet in (f'\x1b[1;{_modifier}{chr(ord("p") + _digit)}',
                        f'\x1b[27;{_modifier};{48 + _digit}~',
                        f'\x1b[{48 + _digit};{_modifier}u'):
            DIGIT_REPORTS[_packet.encode()] = _control


def decode(packet):
    if packet in DIGIT_REPORTS:
        return DIGIT_REPORTS[packet]
    match = re.fullmatch(rb'\x1b\[(90[0-9]|91[0-2]);([2-8])~', packet)
    if not match:
        return packet
    code, modifier = map(int, match.groups())
    # xterm modifier: 1 + Shift(1) + Alt(2) + Ctrl(4).
    if 900 <= code <= 909:
        digit = code - 900
        if modifier == 3:
            return f'\x1b[>{20 + digit}v'.encode()  # Alt: bank
        if modifier == 6 and digit:
            return f'\x1b[>63;{BRIGHTNESS[digit]}v'.encode()
        if modifier == 8 and digit == 0:
            return b'\x1b[>63;0v'
        if modifier == 8 and digit in (1, 2):
            return f'\x1b[>{digit}v'.encode()  # ANSI / VT52
        if modifier == 4 and digit == 3:
            return b'\x1b[>15v'  # RAW toggle
    elif code == 910 and modifier == 3:
        return b'\x1b[>60v'  # configured exit, Alt+Q only
    elif code == 911 and modifier == 6:
        return b'\x1dm'  # host transfer panel, Ctrl+Shift+X only
    elif code == 912 and modifier == 7:
        from key_event_ids import key_ids
        return f'\x1b[>70;1;{key_ids()["KeyC"]};5;v'.encode()
    # Preserve ordinary modified letters if Mintty routes them through the
    # same character tag. Do not turn Ctrl+Q/X/C into application controls.
    if code >= 910:
        letter = 'qxc'[code - 910]
        bits = modifier - 1
        data = bytes([ord(letter) & 31]) if bits & 4 else (letter.upper() if bits & 1 else letter).encode()
        return (b'\x1b' if bits & 2 else b'') + data
    # Unassigned numeric chords keep an explicit standard modified-key report.
    return f'\x1b[27;{modifier};{48 + code - 900}~'.encode()


# Finite packet set bounds buffering and leaves unrelated CSI input alone.
PACKETS = {f'\x1b[{code};{modifier}~'.encode()
           for code in range(900, 913) for modifier in range(2, 9)} | DIGIT_REPORTS.keys()
PREFIXES = {packet[:length] for packet in PACKETS for length in range(1, len(packet))}


class MinttyInput:
    def __init__(self):
        self.pending = bytearray()
        self.at = 0.0

    def feed(self, data):
        result = bytearray()
        for byte in data:
            if not self.pending:
                if byte == 27:
                    self.pending.append(byte)
                    self.at = time.monotonic()
                else:
                    result.append(byte)
                continue
            self.pending.append(byte)
            pending = bytes(self.pending)
            if pending in PACKETS:
                result.extend(decode(pending))
                self.pending.clear()
            elif pending not in PREFIXES:
                # Preserve unrelated input; an adjacent ESC starts a new packet.
                if byte == 27:
                    result.extend(pending[:-1])
                    self.pending[:] = b'\x1b'
                    self.at = time.monotonic()
                else:
                    result.extend(pending)
                    self.pending.clear()
        return bytes(result)

    def tick(self):
        if self.pending and time.monotonic() - self.at >= .05:
            pending = bytes(self.pending)
            self.pending.clear()
            return pending
        return b''
