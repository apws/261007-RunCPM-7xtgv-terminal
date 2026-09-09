#!/usr/bin/env python3
"""PC keyboard and screen bridge for 7xtgv-vt100-runcpm (Linux, Python 3)."""
import argparse
import errno
import fcntl
import glob
import os
import select
import signal
import struct
import sys
import termios
import time
import tty

PREFIX = 0x1D  # Ctrl+]
COMMANDS = {
    ord('1'): b'\x1b[>1v', ord('2'): b'\x1b[>2v',
    ord('a'): b'\x1b[1;3H', ord('e'): b'\x1b[1;3F',
    ord('h'): b'\x1b[1;3D', ord('l'): b'\x1b[1;3C',
    ord('r'): b'\x1b[>0v', PREFIX: bytes([PREFIX]),
    ord('p'): b'\x1b[>10v', ord('n'): b'\x1b[>11v',
    ord('['): b'\x1b[>12v', ord(']'): b'\x1b[>13v',
}

class Keys:
    def __init__(self):
        self.prefix = False
        self.quit = False
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
            if self.prefix:
                self.prefix = False
                if c == ord('q'):
                    self.quit = True
                    break
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


def choose_port(port):
    if port:
        return port
    ports = sorted(glob.glob('/dev/serial/by-id/usb-Generic_RP2350*-if00'))
    if len(ports) != 1:
        raise RuntimeError('Specify --port: expected exactly one RP2350 serial device.')
    return ports[0]


def configure_serial(fd):
    tty.setraw(fd, termios.TCSANOW)
    attrs = termios.tcgetattr(fd)
    attrs[2] |= termios.CLOCAL | termios.CREAD
    attrs[2] &= ~(termios.HUPCL | getattr(termios, 'CRTSCTS', 0))
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    # DTR allows the firmware to detect a connected PC and send a full snapshot.
    fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR))


def interrupted(signum, frame):
    raise KeyboardInterrupt


def run(port, mode):
    if not os.isatty(0) or not os.isatty(1):
        raise RuntimeError('Run this helper inside kitty or Konsole, with an interactive keyboard.')
    dimensions = os.get_terminal_size(1)
    if dimensions.columns < 80 or dimensions.lines < 24:
        raise RuntimeError('Enlarge the PC terminal to at least 80 columns and 24 rows.')
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    saved_serial = None
    saved_keyboard = termios.tcgetattr(0)
    saved_stdout = fcntl.fcntl(1, fcntl.F_GETFL)
    keys = Keys()
    incoming, outgoing = bytearray(), bytearray()
    entered = False
    try:
        # Prevent another serial program from opening this device during the session.
        fcntl.ioctl(fd, termios.TIOCEXCL)
        saved_serial = termios.tcgetattr(fd)
        configure_serial(fd)
        tty.setraw(0, termios.TCSANOW)
        os.write(1, b'\x1b[?1049h\x1b[?2004l\x1b[?1l\x1b>\x1b[0m\x1b[2J\x1b[H')
        entered = True
        fcntl.fcntl(1, fcntl.F_SETFL, saved_stdout | os.O_NONBLOCK)
        if mode:
            outgoing.extend(COMMANDS[ord('1' if mode == 'ansi' else '2')])
        outgoing.extend(b'\x1b[>0v')
        while not keys.quit:
            readers = []
            if len(outgoing) < 8192:
                readers.append(0)
            if len(incoming) < 65536:
                readers.append(fd)
            writers = ([fd] if outgoing else []) + ([1] if incoming else [])
            readable, writable, _ = select.select(readers, writers, [], 0.1)
            outgoing.extend(keys.tick())
            if 0 in readable:
                data = os.read(0, 256)
                if not data:
                    break
                outgoing.extend(keys.feed(data))
            if fd in readable:
                try:
                    data = os.read(fd, 4096)
                except BlockingIOError:
                    data = None
                if data == b'':
                    raise RuntimeError('RP2350 disconnected. Reconnect the device and launch the helper again.')
                if data:
                    incoming.extend(data)
            for target, pending in ((fd, outgoing), (1, incoming)):
                if target in writable:
                    try:
                        written = os.write(target, pending[:4096])
                        del pending[:written]
                    except BlockingIOError:
                        pass
    finally:
        fcntl.fcntl(1, fcntl.F_SETFL, saved_stdout)
        if entered:
            os.write(1, b'\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l')
        termios.tcsetattr(0, termios.TCSANOW, saved_keyboard)
        if saved_serial is not None:
            try:
                fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_DTR))
                termios.tcsetattr(fd, termios.TCSANOW, saved_serial)
                fcntl.ioctl(fd, termios.TIOCNXCL)
            except OSError:
                pass
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        'Ctrl+] then q: exit; 1: ANSI; 2: VT52; a/e: left/right edge; '
        'h/l: pan; r: redraw; p/n: history; [/]: history bank; '
        'Ctrl+] again: literal Ctrl+]. '
        'Alt+Home/End and Alt+Left/Right also pan.'))
    parser.add_argument('--port', help='Serial device; defaults to the unique RP2350 by-id path')
    parser.add_argument('--mode', choices=('ansi', 'vt52'), help='Select mode on connection; otherwise preserve it')
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGHUP, interrupted)
    try:
        run(choose_port(args.port), args.mode)
    except KeyboardInterrupt:
        pass
    except (OSError, RuntimeError) as exc:
        print(f'7xtgv: {exc}', file=sys.stderr)
        if isinstance(exc, PermissionError):
            print('Your user needs read/write access to the serial device. See README.md.', file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
