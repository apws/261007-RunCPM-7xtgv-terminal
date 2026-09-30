#!/usr/bin/env python3
"""PC keyboard and screen bridge for RunCPM_7xtgv-terminal (Linux, Python 3)."""
import argparse
import errno
import fcntl
import grp
import importlib.util
import os
import pwd
import re
import select
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import termios
import tempfile
import textwrap
import time
import traceback
import tty
import unicodedata
from pathlib import Path

# Also support the existing import-by-path host tests and transfer helper.
if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
from target_config import resolve_port

PREFIX = 0x1D  # Ctrl+]
SOH, EOT, ACK, NAK, CAN, CRC_REQUEST = 0x01, 0x04, 0x06, 0x15, 0x18, 0x43
SNAPSHOT_REQUEST = b'\x1b[>62v'  # PC mirror only; does not redraw the TFT.
SNAPSHOT_RESET = b'\x1b[2J\x1b[H'
MOUSE_MODES = (1000, 1002, 1003, 1005, 1006, 1007, 1015, 1016)
TRANSFER_ROOT = Path(os.path.expanduser(os.environ.get(
    'XTGV_TRANSFER_ROOT', os.environ.get('XTGV_XMODEM_ROOT',
    str(Path(__file__).resolve().parents[3] / 'cpmdrive'))))).absolute()
# Guest-to-host capability request.  This is deliberately handled by the PC
# bridge only: the firmware has no knowledge of the host build/upload setup.
HOST_REFLASH_REQUEST = b'\x1b[>64v'
COMMANDS = {
    ord('1'): b'\x1b[>1v', ord('2'): b'\x1b[>2v',
    ord('a'): b'\x1b[1;3H', ord('e'): b'\x1b[1;3F',
    # Preserve the old prefix chords as harmless no-ops. View positions are
    # now calculated from the logical terminal and physical viewport sizes.
    ord('h'): b'', ord('l'): b'',
    ord('r'): b'\x1b[>0v', PREFIX: bytes([PREFIX]),
    ord('g'): b'\x1b[>4v',
    # Retain these ASCII prefix fallbacks for host bridges/keyboards which
    # cannot generate the dedicated modifier chord.
    ord('+'): b'\x1b[>66v', ord('-'): b'\x1b[>67v',
    ord('#'): b'\x1b[>15v',
    ord('x'): b'\x1b[>60v', ord('c'): b'\x1b[>61v',
    ord('p'): b'\x1b[>10v', ord('n'): b'\x1b[>11v', ord('d'): b'\x1b[>65v',
    ord('['): b'\x1b[>12v', ord(']'): b'\x1b[>13v',
}


def mirror_sanitize_host_modes():
    """Keep mouse reports and alternate-scroll out of the CP/M keyboard path."""
    return b''.join(b'\x1b[?' + str(mode).encode('ascii') + b'l'
                    for mode in MOUSE_MODES)


def mirror_init():
    return (b'\x1b[?1049h' + mirror_sanitize_host_modes()
            + b'\x1b[?2004l\x1b[?1l\x1b>\x1b[0m\x1b[2J\x1b[H')


class MouseReports:
    """Discard complete X10, SGR, and urxvt reports; preserve other CSI input."""
    def __init__(self):
        self.pending = bytearray()
        self.pending_at = 0.0

    @staticmethod
    def _decimal_report(data, sgr):
        digits = fields = 0
        for index, byte in enumerate(data):
            if ord('0') <= byte <= ord('9'):
                digits += 1
                if digits > 5:
                    return False
            elif byte == ord(';') and digits and fields < 2:
                fields += 1
                digits = 0
            elif byte in ((ord('M'), ord('m')) if sgr else (ord('M'),)):
                return fields == 2 and digits > 0 and index == len(data) - 1
            else:
                return False
        return None

    @classmethod
    def _status(cls, data):
        if data[0] != 27:
            return False
        if len(data) == 1:
            return None
        if data[1] != ord('['):
            return False
        if len(data) == 2:
            return None
        tail = data[2:]
        if tail[0] == ord('M'):
            # X10: CSI M followed by button, column, row (each offset by 32).
            if any(byte < 32 for byte in tail[1:]) or len(tail) > 4:
                return False
            if len(tail) == 4:
                return tail[2] >= 33 and tail[3] >= 33
            return None
        if tail[0] == ord('<'):
            return cls._decimal_report(tail[1:], sgr=True)
        if ord('0') <= tail[0] <= ord('9'):
            return cls._decimal_report(tail, sgr=False)
        return False

    def feed(self, data):
        passed = bytearray()
        for byte in data:
            self.pending.append(byte)
            self.pending_at = time.monotonic()
            while self.pending:
                status = self._status(self.pending)
                if status is True:
                    self.pending.clear()
                elif status is False or len(self.pending) > 32:
                    passed.append(self.pending.pop(0))
                else:
                    break
        return bytes(passed)

    def tick(self):
        # A lone ESC or incomplete report must never trap ordinary keyboard input.
        if self.pending and time.monotonic() - self.pending_at >= 0.05:
            passed = bytes(self.pending)
            self.pending.clear()
            return passed
        return b''

class Keys:
    def __init__(self):
        self.prefix = False
        self.brightness = False
        self.quit = False
        self.xmodem = False
        keys = os.environ.get('XTGV_BANK_KEYS', '+ěščřžýáíé')
        numbers = os.environ.get('XTGV_BANK_NUMBERS', '1234567890')
        self.bank_keys = dict(zip(keys, numbers))
        self.alt = bytearray()
        self.alt_at = 0.0

    def _feed_alt(self, c):
        self.alt.append(c)
        try:
            char = bytes(self.alt[1:]).decode('utf-8')
        except UnicodeDecodeError as exc:
            if exc.reason == 'unexpected end of data':
                return b''
            char = ''
        number = self.bank_keys.get(char) if len(char) == 1 else None
        if char == '#':
            result = b'\x1b[>15v'
        elif char == ' ':
            result = b'\x1b[>14v'
        else:
            result = b'\x1b[>2' + number.encode('ascii') + b'v' if number else bytes(self.alt)
        self.alt.clear()
        return result

    def tick(self):
        if self.alt and time.monotonic() - self.alt_at >= 0.05:
            result = bytes(self.alt)
            self.alt.clear()
            return result
        return b''

    def feed(self, data):
        result = bytearray()
        for c in data:
            if self.brightness:
                self.brightness = False
                if ord('0') <= c <= ord('9'):
                    # Digit shortcuts select OFF or coarse logical levels,
                    # not brightness percentages; each HAL maps their duties.
                    # PgUp/PgDn step through every lit level (1..31).
                    level = (0, 4, 7, 11, 14, 18, 21, 25, 28, 31)[c - ord('0')]
                    result.extend(b'\x1b[>63;' + str(level).encode('ascii') + b'v')
                elif c == ord('+'):
                    result.extend(b'\x1b[>63;33v')
                elif c == ord('-'):
                    result.extend(b'\x1b[>63;34v')
                else:
                    result.extend(bytes([PREFIX, ord('b'), c]))
            elif self.prefix:
                self.prefix = False
                if c == ord('q'):
                    self.quit = True
                    break
                if c == ord('m'):
                    self.xmodem = True
                    continue
                if c == ord('b'):
                    self.brightness = True
                    continue
                result.extend(COMMANDS.get(c, bytes([PREFIX, c])))
            elif self.alt:
                result.extend(self._feed_alt(c))
            elif c == PREFIX:
                self.prefix = True
            elif c == 27:
                self.alt.append(c)
                self.alt_at = time.monotonic()
            else:
                result.append(c)
        return result


def xmodem_crc(data):
    """XMODEM's CRC-16/CCITT, initial value zero."""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xffff if crc & 0x8000 else (crc << 1) & 0xffff
    return crc


def xmodem_packet(number, payload):
    if len(payload) != 128:
        raise ValueError('XMODEM packet payload must be 128 bytes')
    crc = xmodem_crc(payload)
    return bytes((SOH, number & 0xff, (~number) & 0xff)) + payload + bytes((crc >> 8, crc & 0xff))


class TransferPrompt:
    """Small host-only UI; the CP/M screen is reconstructed on leaving it."""
    def __init__(self, root=None):
        self.direction = None
        self.root = Path(root) if root is not None else TRANSFER_ROOT
        self.stage = 'du'
        self.directory = None
        self.du = ''
        self.error = ''
        self.path = bytearray()
        self.position = 0
        self.escape = bytearray()
        self.escape_at = 0.0
        self.source = ''
        self.target = ''
        self.overwrite = None
        self.overwrite_approved = False

    @staticmethod
    def file_stamp(path):
        if path.is_symlink() or not path.is_file():
            raise ValueError('Target must be a regular file, not a directory or symlink.')
        info = path.stat()
        return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)

    def escape_ready(self):
        return bool(self.escape) and time.monotonic() - self.escape_at >= 0.15

    def chosen_path(self, value):
        if self.directory is None or not value or value in ('.','..') or '/' in value or '\\' in value:
            raise ValueError('Enter a filename only, without a directory.')
        # The CP/M mirror normally uses uppercase filenames; accept typed lowercase.
        matches = [p for p in self.directory.iterdir() if p.name.casefold() == value.casefold()]
        if len(matches) > 1:
            raise ValueError('Ambiguous filename: multiple case variants exist.')
        return matches[0] if matches else self.directory / value.upper()

    def select_directory(self, value):
        match = re.fullmatch(r'([A-P])([0-9A-F]|1[0-5]):?', value.strip().upper())
        if not match:
            raise ValueError('Use DU such as A0:, A6:, AF: (or A15:).')
        user = int(match[2], 10 if len(match[2]) == 2 else 16)
        du = match[1] + format(user, 'X')
        matches = [p for p in self.root.iterdir() if p.is_dir() and
                   (p.name.upper() == du or p.name.upper().startswith(du + ' - '))]
        if not matches:
            raise ValueError(f'No local folder for {du}: under cpmdrive.')
        if len(matches) != 1:
            raise ValueError(f'Multiple local folders match {du}:; resolve ambiguity first.')
        self.du, self.directory = du + ':', matches[0]

    def _left(self):
        if self.position:
            self.position -= 1
            while self.position and self.path[self.position] & 0xc0 == 0x80:
                self.position -= 1

    def _right(self):
        if self.position < len(self.path):
            self.position += 1
            while self.position < len(self.path) and self.path[self.position] & 0xc0 == 0x80:
                self.position += 1

    def _key(self, key):
        self.error = ''
        if key == 'cancel':
            return 'cancel'
        if self.stage == 'done':
            return 'cancel' if key == 'enter' else None
        if self.stage == 'confirm':
            return None
        if self.direction is None:
            return None
        if key == 'left':
            self._left()
        elif key == 'right':
            self._right()
        elif key == 'home':
            self.position = 0
        elif key == 'end':
            self.position = len(self.path)
        elif key == 'backspace':
            end = self.position
            self._left()
            del self.path[self.position:end]
        elif key == 'delete':
            start = self.position
            self._right()
            del self.path[start:self.position]
            self.position = start
        elif key == 'clear':
            self.path.clear()
            self.position = 0
        elif key == 'enter' and self.path:
            value = self.path.decode('utf-8', 'surrogateescape')
            try:
                if self.stage == 'du':
                    self.select_directory(value)
                    self.stage = 'file'
                    self.path.clear()
                    self.position = 0
                elif self.stage in ('file', 'target'):
                    self.chosen_path(value)  # Validate before closing the editor.
                    if not re.fullmatch(r'[A-Za-z0-9_-]{1,8}\.[A-Za-z0-9]{1,3}', value):
                        raise ValueError('Use an 8.3 CP/M filename, e.g. TEST.INI.')
                    if self.stage == 'file':
                        if self.direction == 'send' and not self.chosen_path(value).is_file():
                            raise ValueError('Local source file does not exist.')
                        self.source = value.upper()
                        self.stage = 'target'
                        self.path = bytearray(value.encode('ascii'))
                        self.position = len(self.path)
                        return None
                    self.target = value.upper()
                    self.overwrite = None
                    self.overwrite_approved = False
                    if self.direction == 'receive':
                        destination = self.chosen_path(self.target)
                        if destination.exists() or destination.is_symlink():
                            self.overwrite = (destination, self.file_stamp(destination))
                    if self.direction == 'send' or self.overwrite is not None:
                        self.stage = 'confirm'
                        return None
                    return self.direction, value
            except (OSError, ValueError) as exc:
                self.error = str(exc)
        return None

    def _byte(self, byte):
        if self.stage == 'done':
            return 'cancel' if byte in (13, 10, 32) else None
        if self.stage == 'confirm':
            if byte in (ord('y'), ord('Y')):
                self.overwrite_approved = True
                return self.direction, self.path.decode('utf-8', 'surrogateescape')
            if byte in (ord('n'), ord('N')):
                self.stage = 'target'
            if byte in (ord('c'), ord('C')):
                return 'cancel'
            return None
        controls = {8: 'backspace', 127: 'backspace', 13: 'enter', 10: 'enter',
                    1: 'home', 5: 'end', 21: 'clear', 24: 'clear'}
        if byte in controls:
            return self._key(controls[byte])
        if self.direction is None:
            if byte in (ord('s'), ord('S'), ord('r'), ord('R')):
                self.direction = 'send' if byte in (ord('s'), ord('S')) else 'receive'
                self.path.clear()
                self.position = 0
        elif 32 <= byte < 127 or byte >= 128:
            self.error = ''
            self.path[self.position:self.position] = bytes((byte,))
            self.position += 1
        return None

    def _sequence(self, sequence):
        # Decode the host key vocabulary locally, never through CP/M bindings.
        ordinary = {b'\x1b[D': 'left', b'\x1b[C': 'right',
                    b'\x1b[H': 'home', b'\x1b[F': 'end',
                    b'\x1bOH': 'home', b'\x1bOF': 'end',
                    b'\x1bOD': 'left', b'\x1bOC': 'right',
                    b'\x1b[1~': 'home', b'\x1b[4~': 'end', b'\x1b[3~': 'delete'}
        if sequence in ordinary:
            return self._key(ordinary[sequence])
        legacy = re.fullmatch(rb'\x1b\[>(30|31|32|33|34|65)v', sequence)
        if legacy:
            return self._key({30: 'left',31: 'right',32: 'home',33: 'end',
                              34: 'delete',65: 'clear'}[int(legacy[1])])
        event = re.fullmatch(rb'\x1b\[>70;1;(\d+);(\d+);([0-9]*)v', sequence)
        if event:
            key = int(event[1])
            action = {1: 'left',2: 'right',3: 'home',4: 'end',5: 'delete',
                      11: 'backspace',12: 'cancel',13: 'enter'}.get(key)
            if action:
                return self._key(action)
            fallback = event[3]
            if len(fallback) % 3 == 0:
                values = [int(fallback[i:i+3]) for i in range(0,len(fallback),3)]
                # Printable fallbacks preserve host layout, including punctuation.
                if all(32 <= value <= 255 and value != 127 for value in values):
                    for value in values:
                        result = self._byte(value)
                        if result is not None:
                            return result
        # Unknown function/modifier sequences are ignored, never cancellation.
        return None

    def screen(self, note='', columns=None):
        cursor = b''
        if self.stage == 'done':
            body = ('VTPUT / VTGET — result\r\n\r\n' + self.result +
                    '\r\n\r\nEnter/Esc returns to the CP/M screen.')
        elif self.direction is None:
            body = (
                'VTPUT / VTGET — host transfer\r\n\r\n'
                'S  local file  -> CP/M    (VTPUT; replaces destination)\r\n'
                'R  CP/M file   -> local   (VTGET)\r\n\r\n'
                'Leave CP/M idle at CCP. The helper is started automatically.\r\n'
                'Esc cancels.'
            )
        elif self.stage == 'confirm':
            where = ('device CURRENT drive/user' if self.direction == 'send'
                     else f'local {self.du}')
            verb = 'Write (replace if present)' if self.direction == 'send' else 'Overwrite'
            body = (f'VTPUT / VTGET — confirm target\r\n\r\n'
                    f'{verb} {self.target} in {where}?\r\n'
                    f'Source: {self.source}\r\n'
                    'Y = Yes; N = edit target name; C/Esc = Cancel.\r\n'
                    'Bytes are sent unchanged (binary); no text conversion.')
        else:
            action = 'Send to device' if self.direction == 'send' else 'Receive from device'
            columns = columns or shutil.get_terminal_size((80,24)).columns
            label = 'DU: ' if self.stage == 'du' else 'Source: ' if self.stage == 'file' else 'Target: '
            width = max(1, columns - len(label) - 2)
            text = self.path.decode('utf-8', 'replace')
            caret = len(self.path[:self.position].decode('utf-8', 'replace'))
            def cells(s):
                return sum(0 if unicodedata.combining(c) else
                           2 if unicodedata.east_asian_width(c) in ('W','F') else 1 for c in s)
            start = 0
            while start < caret and cells(text[start:caret]) >= width:
                start += 1
            end = start
            while end < len(text) and cells(text[start:end+1]) <= width:
                end += 1
            explanation = ('Local mirror area: A0:, A6:, AF: (A15: also accepted).'
                           if self.stage == 'du' else
                           f'Local folder: {self.directory.name} [{self.du}]')
            body = (f'VTPUT / VTGET — {action}\r\n\r\n{label}{text[start:end]}\r\n\r\n'
                    + explanation + '\r\n'
                    'DU selects the LOCAL folder only; device stays in CURRENT DU.\r\n'
                    + (f'Source: {self.source}\r\n' if self.source else '') +
                    'Enter starts; Esc cancels; Ctrl+U clears.\r\n'
                    'Left/Right Home/End BS/Delete edit; long names scroll.'
                    + ('\r\n' + self.error if self.error else ''))
            if self.stage == 'du':
                body = body.replace('Enter starts;', 'Enter selects folder;')
            if not note:
                column = len(label) + 1 + cells(text[start:caret])
                cursor = f'\x1b[3;{column}H'.encode('ascii')
        # stdout is raw and the mirror disables autowrap: LF alone stair-steps,
        # and long error/path messages would be clipped. Wrap and emit CRLF.
        columns = columns or shutil.get_terminal_size((80,24)).columns
        def formatted(text):
            lines = []
            for line in text.splitlines():
                lines.extend(textwrap.wrap(line, max(1, columns-1),
                                          replace_whitespace=False) or [''])
            return '\r\n'.join(lines).encode('utf-8')
        return (b'\x1b[?25h\x1b[0m\x1b[2J\x1b[H\x1b[30;46m' + formatted(body) + b'\x1b[0m'
                + (formatted(note) if note else b'') + cursor)

    def feed(self, data):
        """Return (direction, path), 'cancel', or None."""
        if not data and self.escape_ready():
            lone_escape = self.escape == b'\x1b'
            self.escape.clear()
            return 'cancel' if lone_escape else None
        for byte in data:
            if self.escape:
                self.escape.append(byte)
                self.escape_at = time.monotonic()
                if len(self.escape) == 2 and byte in (ord('['),ord('O')):
                    continue
                if len(self.escape) >= 3 and self.escape[1] in (ord('['),ord('O')):
                    if not 0x40 <= byte <= 0x7e and len(self.escape) < 96:
                        continue
                    result = self._sequence(bytes(self.escape))
                else:
                    result = None
                self.escape.clear()
            elif byte == 27:
                self.escape.append(byte)
                self.escape_at = time.monotonic()
                continue
            else:
                result = self._byte(byte)
            if result is not None:
                return result
        return None


def load_console_transfer():
    spec = importlib.util.spec_from_file_location(
        'vt_console_transfer', Path(__file__).parent / 'cpm-transfer' / 'transfer.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def vt_transfer(fd, prompt, status):
    """Borrow the mirror fd exclusively; never open a second serial reader."""
    for name in (prompt.source, prompt.target):
        if not re.fullmatch(r'[A-Z0-9_-]{1,8}\.[A-Z0-9]{1,3}', name):
            raise ValueError('Use an 8.3 CP/M filename.')
    path = prompt.chosen_path(prompt.source if prompt.direction == 'send' else prompt.target)
    def check_destination():
        if prompt.overwrite is not None:
            if not prompt.overwrite_approved or prompt.overwrite != (path, prompt.file_stamp(path)):
                raise FileExistsError('Local target changed or overwrite was not confirmed; choose it again.')
        elif path.exists() or path.is_symlink():
            raise FileExistsError('Local target now exists; choose it again to confirm overwrite.')
    if prompt.direction == 'receive':
        check_destination()
    elif not prompt.overwrite_approved:
        raise ValueError('Device write was not confirmed.')
    data = path.read_bytes() if prompt.direction == 'send' else None
    module = load_console_transfer()
    link = module.Link(fd=fd)

    class Progress:
        count = 0
        def advance(self, size, filename, phase):
            self.count += size
            if data is None:
                status(f'VTGET {filename}: {self.count} bytes')
                return 0
            total = max(128, (len(data) + 127) // 128 * 128)
            percent = min(100, self.count * 100 // total)
            status(f'VTPUT {filename}: {percent}% ({self.count}/{total} bytes)')
            return percent

    try:
        status('Checking idle CCP prompt…')
        try:
            link.require_ccp()
        except TimeoutError:
            raise RuntimeError('No idle CCP prompt. Exit the application before transfer.') from None
        link.prepare()
        progress = Progress()
        if data is not None:
            size = link.put(prompt.target, data, progress)
            return f'Saved {prompt.target} in device current DU: {size} bytes (record padding included).'
        data = link.get(prompt.source, progress)
        check_destination()
        if prompt.overwrite is None:
            with path.open('xb') as output:
                output.write(data)
        else:
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.vtget-', delete=False) as output:
                    temporary = Path(output.name)
                    output.write(data)
                    output.flush()
                    os.fsync(output.fileno())
                check_destination()
                os.replace(temporary, path)
            finally:
                if temporary is not None and temporary.exists():
                    temporary.unlink()
        return f'Received {prompt.source} as local {prompt.du}{prompt.target}: {len(data)} bytes (record padding retained).'
    finally:
        link.close()  # Borrowed fd stays open; restore original profile/RAW state.


class Xmodem:
    """Protocol test utility only. NOT usable on the reconstructed screen mirror."""
    Retries = 10

    def __init__(self, fd, status):
        self.fd = fd
        self.status = status
        self.pending = bytearray()

    def write(self, data):
        view = memoryview(data)
        while view:
            if not select.select([], [self.fd], [], 5)[1]:
                raise TimeoutError('serial write timed out')
            view = view[os.write(self.fd, view):]

    def read(self, timeout):
        if self.pending:
            return self.pending.pop(0)
        if not select.select([self.fd], [], [], timeout)[0]:
            return None
        data = os.read(self.fd, 4096)
        if not data:
            raise RuntimeError('serial disconnected')
        self.pending.extend(data)
        return self.pending.pop(0)

    def abort(self):
        self.write(bytes((CAN, CAN)))

    def wait_for(self, accepted, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            byte = self.read(min(1.0, deadline - time.monotonic()))
            if byte in accepted:
                return byte
        return None

    def send(self, path):
        data = path.read_bytes()
        self.status(f'Waiting for CP/M receiver — {path.name} ({len(data)} bytes)')
        start = self.wait_for((CRC_REQUEST, NAK, CAN), 60)
        if start == CAN:
            raise RuntimeError('CP/M cancelled transfer')
        if start not in (CRC_REQUEST, NAK):
            raise TimeoutError('CP/M receiver did not start XMODEM')
        crc = start == CRC_REQUEST
        blocks = max(1, (len(data) + 127) // 128)
        for index in range(blocks):
            payload = data[index * 128:(index + 1) * 128].ljust(128, b'\x1a')
            packet = xmodem_packet(index + 1, payload) if crc else (
                bytes((SOH, (index + 1) & 0xff, (~(index + 1)) & 0xff)) + payload
                + bytes((sum(payload) & 0xff,)))
            for attempt in range(self.Retries):
                self.status(f'Sending {path.name}: block {index + 1}/{blocks}' +
                            (f' (retry {attempt})' if attempt else ''))
                self.write(packet)
                reply = self.wait_for((ACK, NAK, CAN), 10)
                if reply == ACK:
                    break
                if reply == CAN:
                    raise RuntimeError('CP/M cancelled transfer')
            else:
                raise TimeoutError(f'CP/M did not acknowledge block {index + 1}')
        for attempt in range(self.Retries):
            self.write(bytes((EOT,)))
            reply = self.wait_for((ACK, CAN), 10)
            if reply == ACK:
                return len(data)
            if reply == CAN:
                raise RuntimeError('CP/M cancelled transfer')
        raise TimeoutError('CP/M did not acknowledge end of transfer')

    def receive(self, path):
        if path.exists():
            raise FileExistsError(f'Refusing to overwrite {path}')
        temporary = path.with_name(path.name + '.part')
        if temporary.exists():
            raise FileExistsError(f'Refusing to overwrite unfinished {temporary}')
        path.parent.mkdir(parents=True, exist_ok=True)
        expected = 1
        received = 0
        with temporary.open('xb') as output:
            try:
                for attempt in range(self.Retries):
                    self.status(f'Waiting for CP/M sender — {path.name}')
                    self.write(bytes((CRC_REQUEST,)))
                    first = self.wait_for((SOH, EOT, CAN), 1.5)
                    if first is not None:
                        break
                else:
                    raise TimeoutError('CP/M sender did not start XMODEM')
                while True:
                    if first == EOT:
                        self.write(bytes((ACK,)))
                        break
                    if first == CAN:
                        raise RuntimeError('CP/M cancelled transfer')
                    header = bytearray()
                    while len(header) < 132:
                        byte = self.read(10)
                        if byte is None:
                            break
                        header.append(byte)
                    if len(header) != 132:
                        self.write(bytes((NAK,)))
                    else:
                        number, complement = header[0], header[1]
                        payload, given_crc = header[2:130], (header[130] << 8) | header[131]
                        if (number ^ complement) != 0xff or xmodem_crc(payload) != given_crc:
                            self.write(bytes((NAK,)))
                        elif number == expected:
                            output.write(payload)
                            received += len(payload)
                            expected = (expected + 1) & 0xff
                            self.status(f'Receiving {path.name}: {received} bytes')
                            self.write(bytes((ACK,)))
                        elif number == ((expected - 1) & 0xff):
                            self.write(bytes((ACK,)))
                        else:
                            self.write(bytes((CAN, CAN)))
                            raise RuntimeError('unexpected XMODEM block number')
                    first = self.wait_for((SOH, EOT, CAN), 10)
                    if first is None:
                        self.write(bytes((NAK,)))
                        first = self.wait_for((SOH, EOT, CAN), 10)
                        if first is None:
                            raise TimeoutError('CP/M sender stalled')
            except BaseException:
                # Preserve a recoverable partial file rather than silently discarding evidence.
                raise
        temporary.rename(path)
        return received


class HostEvents:
    """Remove reserved guest-to-host events from the displayed byte stream."""
    def __init__(self):
        self.pending = bytearray()

    def feed(self, data):
        displayed = bytearray()
        reflash = False
        for byte in data:
            self.pending.append(byte)
            while self.pending:
                identity_prefix = b'\x1bP7xtgv-id;1;'
                if identity_prefix.startswith(self.pending):
                    break
                if self.pending.startswith(identity_prefix) and len(self.pending) <= 160:
                    if self.pending.endswith(b'\x1b\\'):
                        self.pending.clear()
                    break
                if HOST_REFLASH_REQUEST.startswith(self.pending):
                    if self.pending == HOST_REFLASH_REQUEST:
                        reflash = True
                        self.pending.clear()
                    break
                displayed.append(self.pending.pop(0))
        return bytes(displayed), reflash


def choose_port(port, target=None):
    return resolve_port(port, target)


def ensure_port_group(port):
    """Enter the configured device group when this login predates membership."""
    try:
        device_gid = os.stat(port).st_gid
        current_groups = set(os.getgroups()) | {os.getgid(), os.getegid()}
        account = pwd.getpwuid(os.getuid())
        configured_groups = set(os.getgrouplist(account.pw_name, account.pw_gid))
        newgrp = shutil.which('newgrp')
        if device_gid in current_groups or device_gid not in configured_groups or not newgrp:
            return
        group_name = grp.getgrgid(device_gid).gr_name
    except (KeyError, OSError):
        return
    command = shlex.join([sys.executable, os.path.realpath(__file__), *sys.argv[1:]])
    os.execv(newgrp, [newgrp, group_name, '-c', command])


def port_identity(port):
    """Identity changes when udev replaces a native-USB serial device node."""
    status = os.stat(port)
    return status.st_dev, status.st_ino, status.st_rdev


def configure_serial(fd):
    tty.setraw(fd, termios.TCSANOW)
    attrs = termios.tcgetattr(fd)
    attrs[2] |= termios.CLOCAL | termios.CREAD
    attrs[2] &= ~(termios.HUPCL | getattr(termios, 'CRTSCTS', 0))
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    # DTR allows the firmware to detect a connected PC and send a full snapshot.
    fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR))


def open_serial(port):
    """Open one USB-serial incarnation without disturbing the PC terminal."""
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        fcntl.ioctl(fd, termios.TIOCEXCL)
        saved_serial = termios.tcgetattr(fd)
        configure_serial(fd)
        return fd, saved_serial
    except BaseException:
        os.close(fd)
        raise


def close_serial(fd, saved_serial):
    if fd is None:
        return
    try:
        if saved_serial is not None:
            fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_DTR))
            termios.tcsetattr(fd, termios.TCSANOW, saved_serial)
        fcntl.ioctl(fd, termios.TIOCNXCL)
    except OSError:
        # A USB disconnect invalidates the old file descriptor.
        pass
    os.close(fd)


def interrupted(signum, frame):
    raise KeyboardInterrupt


def run(port, mode, reflash_command=None):
    if not os.isatty(0) or not os.isatty(1):
        raise RuntimeError('Run this helper inside kitty or Konsole, with an interactive keyboard.')
    dimensions = os.get_terminal_size(1)
    if dimensions.columns < 80 or dimensions.lines < 24:
        raise RuntimeError('Enlarge the PC terminal to at least 80 columns and 24 rows.')
    fd = None
    saved_serial = None
    saved_keyboard = termios.tcgetattr(0)
    saved_stdout = fcntl.fcntl(1, fcntl.F_GETFL)
    keys = Keys()
    mouse_reports = MouseReports()
    host_events = HostEvents()
    incoming, outgoing = bytearray(), bytearray()
    xmodem_prompt = None
    entered = False
    try:
        fd, saved_serial = open_serial(port)
        opened_identity = port_identity(port)
        tty.setraw(0, termios.TCSANOW)
        os.write(1, mirror_init())
        entered = True
        fcntl.fcntl(1, fcntl.F_SETFL, saved_stdout | os.O_NONBLOCK)
        if mode:
            outgoing.extend(COMMANDS[ord('1' if mode == 'ansi' else '2')])
        outgoing.extend(SNAPSHOT_REQUEST)
        snapshot_pending = True
        snapshot_retry_at = time.monotonic() + 2.0
        snapshot_probe = bytearray()
        reconnect_at = 0.0
        identity_check_at = time.monotonic() + 0.25
        host_dimensions = dimensions
        resize_seen_at = 0.0
        while not keys.quit:
            # Keep stdin readable during a USB outage so Ctrl+] q always works.
            readers = [0]
            if fd is not None and len(incoming) < 65536:
                readers.append(fd)
            writers = ([fd] if fd is not None and outgoing else []) + ([1] if incoming else [])
            try:
                readable, writable, _ = select.select(readers, writers, [], 0.1)
            except (OSError, ValueError):
                # Native USB may invalidate the descriptor before it reports
                # EOF.  Keep the PC alternate screen and treat this exactly
                # like a normal serial disconnect.
                close_serial(fd, saved_serial)
                fd = saved_serial = None
                reconnect_at = time.monotonic() + 0.25
                continue
            outgoing.extend(keys.feed(mouse_reports.tick()))
            outgoing.extend(keys.tick())
            # Konsole/Kitty font changes usually change this PTY's cell
            # geometry.  The device cannot know that newly visible host rows
            # were previously clipped, so request a clean reconstructed mirror
            # after the resize has settled.  Do not resize the host ourselves.
            current_dimensions = os.get_terminal_size(1)
            if current_dimensions != host_dimensions:
                host_dimensions = current_dimensions
                resize_seen_at = time.monotonic()
            elif resize_seen_at and time.monotonic() - resize_seen_at >= 0.15:
                resize_seen_at = 0.0
                snapshot_pending = True
                snapshot_probe.clear()
                snapshot_retry_at = time.monotonic()
            if fd is not None and time.monotonic() >= identity_check_at:
                identity_check_at = time.monotonic() + 0.25
                try:
                    replaced = port_identity(port) != opened_identity
                except OSError:
                    replaced = True
                if replaced:
                    close_serial(fd, saved_serial)
                    fd = saved_serial = None
                    reconnect_at = time.monotonic() + 0.05
            if 0 in readable or (xmodem_prompt is not None and xmodem_prompt.escape_ready()):
                data = os.read(0, 256) if 0 in readable else b''
                if 0 in readable and not data:
                    break
                if xmodem_prompt is not None:
                    answer = xmodem_prompt.feed(data)
                    if answer == 'cancel':
                        xmodem_prompt = None
                        incoming.clear()
                        snapshot_pending = True
                        snapshot_retry_at = time.monotonic()
                    elif answer is not None:
                        direction, raw_path = answer

                        def xmodem_status(message):
                            os.write(1, xmodem_prompt.screen('\r\n' + message))

                        try:
                            if fd is None or outgoing:
                                raise RuntimeError('Connection not idle; return to CCP and try again.')
                            xmodem_prompt.stage = 'busy'
                            result = vt_transfer(fd, xmodem_prompt, xmodem_status)
                        except (OSError, RuntimeError, TimeoutError, ValueError) as exc:
                            detail = '\n'.join(line.strip() for line in str(exc).splitlines() if line.strip())
                            result = ('Transfer failed: ' + detail +
                                      '\nAt CCP: no reset needed. Reset only if a helper is still active.')
                        xmodem_prompt.stage = 'done'
                        xmodem_prompt.result = result
                        os.write(1, xmodem_prompt.screen())
                        incoming.clear()
                        snapshot_pending = False
                        snapshot_probe.clear()
                        snapshot_retry_at = time.monotonic()
                    else:
                        os.write(1, xmodem_prompt.screen())
                else:
                    translated = keys.feed(mouse_reports.feed(data))
                    if keys.xmodem:
                        keys.xmodem = False
                        xmodem_prompt = TransferPrompt()
                        incoming.clear()
                        snapshot_pending = False
                        os.write(1, xmodem_prompt.screen())
                    if not keys.quit and len(outgoing) < 8192:
                        outgoing.extend(translated)
            if fd is None and time.monotonic() >= reconnect_at:
                reconnect_at = time.monotonic() + 0.25
                try:
                    fd, saved_serial = open_serial(port)
                except OSError:
                    pass
                else:
                    opened_identity = port_identity(port)
                    # The firmware answers this with its complete text buffer.
                    # Do not invalidate the TFT: a completed xterm256 plot stays up.
                    incoming.clear()
                    incoming.extend(mirror_sanitize_host_modes())
                    snapshot_pending = True
                    snapshot_probe.clear()
                    snapshot_retry_at = time.monotonic()
            if fd is not None and snapshot_pending and time.monotonic() >= snapshot_retry_at:
                outgoing.extend(SNAPSHOT_REQUEST)
                snapshot_retry_at = time.monotonic() + 2.0
            if fd in readable:
                try:
                    data = os.read(fd, 4096)
                except BlockingIOError:
                    data = None
                if data == b'':
                    close_serial(fd, saved_serial)
                    fd = saved_serial = None
                    reconnect_at = time.monotonic() + 0.25
                if data:
                    data, reflash_requested = host_events.feed(data)
                    if xmodem_prompt is not None:
                        # The transfer panel owns the host display. A snapshot
                        # restores the CP/M screen after the result is dismissed.
                        continue
                    if snapshot_pending:
                        snapshot_probe.extend(data)
                        if SNAPSHOT_RESET in snapshot_probe:
                            snapshot_pending = False
                            snapshot_probe.clear()
                        elif len(snapshot_probe) > len(SNAPSHOT_RESET):
                            del snapshot_probe[:-len(SNAPSHOT_RESET)]
                    incoming.extend(data)
                    if reflash_requested:
                        if not reflash_command:
                            incoming.extend(b'\r\n[7xtgv: REFLASH requested, but no host uploader is configured]\r\n')
                        else:
                            # Release the exclusive serial lock before the
                            # configured uploader resets or replaces the USB
                            # device.  The normal reconnect/snapshot logic
                            # resumes this same mirror afterwards.
                            close_serial(fd, saved_serial)
                            fd = saved_serial = None
                            incoming.extend(b'\r\n[7xtgv: host reflash requested]\r\n')
                            try:
                                # terminal.py owns Konsole's raw alternate
                                # screen.  Upload progress written there would
                                # be interpreted as terminal text and corrupt
                                # the mirror, so retain it in a normal log.
                                with open('/tmp/7xtgv-reflash.log', 'ab') as log:
                                    log.write(time.strftime('\n%F %T reflash\n').encode())
                                    log.flush()  # Keep the request ahead of child upload output.
                                    subprocess.run(reflash_command, check=True,
                                                   stdout=log, stderr=subprocess.STDOUT)
                            except (OSError, subprocess.CalledProcessError) as exc:
                                incoming.extend(f'[7xtgv: host reflash failed: {exc}]\r\n'.encode())
                            reconnect_at = time.monotonic() + 0.25
            for target, pending in ((fd, outgoing), (1, incoming)):
                if target is not None and target in writable:
                    try:
                        written = os.write(target, pending[:4096])
                        del pending[:written]
                    except BlockingIOError:
                        pass
                    except OSError:
                        if target == fd:
                            close_serial(fd, saved_serial)
                            fd = saved_serial = None
                            reconnect_at = time.monotonic() + 0.25
                        else:
                            raise
    finally:
        fcntl.fcntl(1, fcntl.F_SETFL, saved_stdout)
        if entered:
            os.write(1, mirror_sanitize_host_modes()
                     + b'\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l')
        termios.tcsetattr(0, termios.TCSANOW, saved_keyboard)
        close_serial(fd, saved_serial)


def main():
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        'Ctrl+] then q: exit; 1: ANSI; 2: VT52; a/e: previous/next horizontal view; '
        'r: redraw; g: xterm256 GCLS; p/n: history; [/]: history bank; '
        '#: toggle RAW; x: profile exit; c: profile clear; m: VTPUT/VTGET panel; Ctrl+] again: literal Ctrl+]. '
        'Ctrl+Shift+arrows pan; Home/End scan all views; '
        'Ctrl+Shift+PgDn zooms in, PgUp zooms out; add Alt for backlight.'))
    parser.add_argument('--port', help='Serial device; otherwise XTGV_PORT or selected target data')
    parser.add_argument('--target', help='Target for port defaults; otherwise XTGV_TARGET')
    parser.add_argument('--mode', choices=('ansi', 'vt52'), help='Select mode on connection; otherwise preserve it')
    parser.add_argument('--reflash-command',
                        help='opt-in host uploader invoked by guest CSI >64v; overrides XTGV_REFLASH_COMMAND')
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGHUP, interrupted)
    try:
        port = choose_port(args.port, args.target)
        # The device-triggered uploader must inherit this actual selection,
        # including explicit CLI overrides, not a stale launcher environment.
        if args.target:
            os.environ['XTGV_TARGET'] = args.target
        os.environ['XTGV_PORT'] = port
        ensure_port_group(port)
        command = args.reflash_command or os.environ.get('XTGV_REFLASH_COMMAND')
        run(port, args.mode, shlex.split(command) if command else None)
    except KeyboardInterrupt:
        pass
    except (OSError, RuntimeError) as exc:
        # USB disconnects should be handled inside run().  Preserve a traceback
        # for any remaining exit path; tmux otherwise disappears with only a
        # blank Konsole alternate screen left behind.
        with open('/tmp/7xtgv-terminal.log', 'a', encoding='utf-8') as log:
            log.write(time.strftime('%F %T ') + repr(exc) + '\n')
            traceback.print_exc(file=log)
        print(f'7xtgv: {exc}', file=sys.stderr)
        if isinstance(exc, PermissionError):
            print('Your user needs read/write access to the serial device. See README.md.', file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
