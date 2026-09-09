#!/usr/bin/env python3
"""Opt-in hardware smoke test. Uses transient MBASIC lines; writes no SD files."""
import argparse
import codecs
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import re
import select
import termios
import time

spec = importlib.util.spec_from_file_location('bridge', Path(__file__).parents[1] / 'tools' / 'terminal.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class Mirror:
    def __init__(self, row_count=24):
        self.row_count = row_count
        self.rows = [[' '] * 80 for _ in range(row_count)]
        self.x = self.y = 0
        self.escape = None
        self.decoder = codecs.getincrementaldecoder('utf-8')('replace')

    def feed(self, data):
        for c in self.decoder.decode(data):
            if self.escape is not None:
                self.escape += c
                if self.escape == '[':
                    continue
                if self.escape.startswith('[') and '@' <= c <= '~':
                    body = self.escape[1:-1]
                    if c == 'H':
                        p = [int(x or '1') for x in body.split(';')]
                        self.y = max(0, min(self.row_count - 1, p[0] - 1))
                        self.x = max(0, min(79, (p[1] if len(p) > 1 else 1) - 1))
                    elif c == 'J' and body == '2':
                        self.rows = [[' '] * 80 for _ in range(self.row_count)]
                    self.escape = None
                elif len(self.escape) > 128:
                    self.escape = None
                continue
            if c == '\x1b':
                self.escape = ''
            elif c == '\r':
                self.x = 0
            elif c == '\n':
                self.y = min(self.row_count - 1, self.y + 1)
            elif c >= ' ':
                self.rows[self.y][self.x] = c
                self.x = min(79, self.x + 1)

    def text(self):
        return '\n'.join(''.join(row).rstrip() for row in self.rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--log', default='/tmp/7xtgv-hardware-smoke.json')
    parser.add_argument('--rows', type=int, default=24)
    args = parser.parse_args()
    reports = []
    fd = None
    screen = Mirror(args.rows)

    def pump(seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            if select.select([fd], [], [], min(0.05, max(0, until-time.monotonic())))[0]:
                data = os.read(fd, 4096)
                if not data:
                    raise RuntimeError('Serial disconnected')
                screen.feed(data)

    def send(data):
        pending = memoryview(data)
        while pending:
            if select.select([], [fd], [], 1)[1]:
                n = os.write(fd, pending)
                pending = pending[n:]

    def expect(label, needle, seconds=5):
        deadline = time.monotonic() + seconds
        while needle not in screen.text() and time.monotonic() < deadline:
            pump(0.1)
        pump(0.3)
        text = screen.text()
        reports.append({'check': label, 'passed': needle in text, 'screen': text})
        if needle not in text:
            raise AssertionError(f'{label}: missing {needle!r}\n{text}')

    def connect():
        port = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        fcntl.ioctl(port, termios.TIOCEXCL)
        bridge.configure_serial(port)
        return port

    def disconnect():
        fcntl.ioctl(fd, termios.TIOCMBIC, __import__('struct').pack('I', termios.TIOCM_DTR))
        fcntl.ioctl(fd, termios.TIOCNXCL)
        os.close(fd)

    try:
        fd = connect()
        send(b'\x1b[>0v')
        expect('boot without PC and attach', 'A0>', 10)
        send(b'DIR\r')
        expect('DIR lists SD files', 'MBASIC')
        pump(1)
        send(b'VTCPU\r')
        expect('VTCPU active measurement', 'VTCPU active:', 10)
        expect('VTCPU quiet measurement', 'VTCPU quiet :', 10)
        speeds = re.findall(r'VTCPU (active|quiet)\s*:.*?=\s*(\d+\.\d{2}) MHz', screen.text())
        if {name for name, _ in speeds} != {'active', 'quiet'}:
            raise AssertionError(f'incomplete VTCPU results: {speeds!r}\n{screen.text()}')
        reports.append({'check': 'VTCPU results', 'passed': True,
                        'MHz': {name: value for name, value in speeds}})
        send(b'MBASIC\r')
        expect('launch MBASIC', 'BASIC-80', 10)
        send(b'PRINT 2+2\r')
        expect('BASIC evaluates arithmetic', '\n 4')
        # Replace the screen before checking fixture markers, so command echo cannot pass.
        send(b'PRINT CHR$(27);"[2J";CHR$(27);"[H";CHR$(27);"[31;4m";"ANSI-";"OK";CHR$(27);"[0m"\r')
        expect('ANSI erase, positioning and text', 'ANSI-OK')
        send(b'PRINT CHR$(27);"[6;71H";"RIGHT-";"EDGE"\r')
        expect('logical column 71', 'RIGHT-EDGE')
        assert ''.join(screen.rows[5][70:80]) == 'RIGHT-EDGE'
        send(b'\x1b[1;3F\x1b[1;3D\x1b[1;3C\x1b[1;3H')
        pump(0.5)
        send(b'PRINT 3+4\r')
        expect('pan controls did not enter CP/M command input', '\n 7')
        send(b'PRINT CHR$(27);"[?2l";CHR$(27);"H";CHR$(27);"J";CHR$(27);"Y";CHR$(40);CHR$(42);"VT52-";"OK";CHR$(27);"<"\r')
        expect('VT52 positioning and ANSI return', 'VT52-OK')
        assert ''.join(screen.rows[8][10:17]) == 'VT52-OK'
        send(b'SYSTEM\r')
        expect('return to CP/M', 'A0>')
        disconnect();fd = None
        time.sleep(0.3)
        screen = Mirror(args.rows);fd = connect();send(b'\x1b[>0v')
        expect('reconnect retains session', 'A0>')
        assert 'BASIC-80' not in screen.text()
        print('PASS: CP/M DIR, VTCPU, MBASIC, ANSI/VT52, pan keys, and reconnect')
    finally:
        Path(args.log).write_text(json.dumps(reports, indent=2)+'\n')
        if fd is not None:
            disconnect()

if __name__ == '__main__':
    main()
