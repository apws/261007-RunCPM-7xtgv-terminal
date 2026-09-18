#!/usr/bin/env python3
"""PC keyboard and screen bridge for 7xtgv-vt100-runcpm (Linux, Python 3)."""
import argparse
import errno
import fcntl
import glob
import grp
import os
import pwd
import select
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import termios
import time
import traceback
import tty

PREFIX = 0x1D  # Ctrl+]
SNAPSHOT_REQUEST = b'\x1b[>62v'  # PC mirror only; does not redraw the TFT.
SNAPSHOT_RESET = b'\x1b[2J\x1b[H'
MOUSE_MODES = (1000, 1002, 1003, 1005, 1006, 1007, 1015, 1016)
# Guest-to-host capability request.  This is deliberately handled by the PC
# bridge only: the firmware has no knowledge of the host build/upload setup.
HOST_REFLASH_REQUEST = b'\x1b[>64v'
COMMANDS = {
    ord('1'): b'\x1b[>1v', ord('2'): b'\x1b[>2v',
    ord('a'): b'\x1b[1;3H', ord('e'): b'\x1b[1;3F',
    # Preserve the old prefix chords as harmless no-ops. Partial 4-column
    # panning has been retired in favour of the two global edge jumps.
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
                    # The digit shortcuts are coarse 0/off, 20%, 30% ...
                    # 100%; PgUp/PgDn carry the full 32-level precision.
                    level = (0, 4, 7, 11, 14, 18, 21, 25, 28, 32)[c - ord('0')]
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
                if HOST_REFLASH_REQUEST.startswith(self.pending):
                    if self.pending == HOST_REFLASH_REQUEST:
                        reflash = True
                        self.pending.clear()
                    break
                displayed.append(self.pending.pop(0))
        return bytes(displayed), reflash


def choose_port(port):
    if port:
        return port
    ports = sorted(glob.glob('/dev/serial/by-id/usb-Generic_RP2350*-if00'))
    if len(ports) != 1:
        raise RuntimeError('Specify --port: expected exactly one RP2350 serial device.')
    return ports[0]


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
            if 0 in readable:
                data = os.read(0, 256)
                if not data:
                    break
                translated = keys.feed(mouse_reports.feed(data))
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
        'Ctrl+] then q: exit; 1: ANSI; 2: VT52; a/e: left/right edge; '
        'r: redraw; g: xterm256 GCLS; p/n: history; [/]: history bank; '
        '#: toggle RAW; x: profile exit; c: profile clear; Ctrl+] again: literal Ctrl+]. '
        'Ctrl+Shift+Home/End jump to viewport edges; Ctrl+Alt+Shift+PgUp/PgDn changes glyph zoom.'))
    parser.add_argument('--port', help='Serial device; defaults to the unique RP2350 by-id path')
    parser.add_argument('--mode', choices=('ansi', 'vt52'), help='Select mode on connection; otherwise preserve it')
    parser.add_argument('--reflash-command',
                        help='opt-in host uploader invoked by guest CSI >64v; overrides XTGV_REFLASH_COMMAND')
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGHUP, interrupted)
    try:
        port = choose_port(args.port)
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
