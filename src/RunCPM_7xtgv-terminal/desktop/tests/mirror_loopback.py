"""Exercise the actual mirror/Windows owner on retained disposable guest cards.

MSYS Python on Windows, or Linux with --build-dir selecting a local build.
This drives a PTY; physical Mintty key translation still needs native testing.
"""
import argparse
import contextlib
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time

SKETCH = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SKETCH / 'tools-win'))
from windows_build import OUTPUT, REPO, TARGETS, target_name
from desktop_card_lock import LOCK_NAME, card_lock


class Case:
    def __init__(self, root, build, target, mode):
        self.root = root / mode
        self.card = self.root / 'card with spaces žluťoučký'
        area = self.card / 'A/0'
        area.mkdir(parents=True)
        (area / '7XTGVTRM.INI').write_bytes(
            (f'[display:{TARGETS[target]["display_id"]}]\r\nview_zoom=1\r\n'
             'zoom1_terminal=80x24\r\n[global]\r\nccp_bank=0\r\n'
             '[bank0]\r\ninput=ansi\r\n[bank9]\r\ninput=ansi\r\n').encode() + b'\x1a')
        (area / 'VTHELLO.COM').write_bytes(bytes.fromhex('1110010e09cd05001e580e04cd0500c9') + b'Mirror guest works\r\n$')
        (area / 'VTLOOP.COM').write_bytes(bytes.fromhex('c30001'))
        (area / 'VTFLOOD.COM').write_bytes(bytes.fromhex('1e580e02cd0500c30001'))
        (area / 'VTREBOOT.COM').write_bytes(bytes.fromhex('3e01d37cc9'))
        for name in ('VTPUT.COM', 'VTGET.COM'):
            shutil.copyfile(REPO / 'cpmcard/A/0' / name, area / name)
        self.session = self.root / 'session'
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack('HHHH', 25, 80, 0, 0))
        self.original = termios.tcgetattr(self.slave)
        self.child = subprocess.Popen([sys.executable, str(SKETCH / 'tools-win/windows_run.py'),
                                      'serve', '--target', target, '--build-dir', str(build),
                                      '--session-dir', str(self.session), '--card-root', str(self.card),
                                      '--headless', '--log', str(self.root / 'desktop-local.log')], stdin=self.slave, stdout=self.slave, stderr=self.slave)
        self.output = bytearray()

    def drain(self, seconds=.1):
        if self.master is None:
            time.sleep(seconds)
            return
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([self.master], [], [], .03)[0]:
                try:
                    chunk = os.read(self.master, 65536)
                except OSError:
                    break
                self.output.extend(chunk)

    def until(self, marker, timeout=10):
        start = len(self.output)
        end = time.monotonic() + timeout
        while marker not in self.output[start:] and time.monotonic() < end:
            self.drain()
            if self.child.poll() is not None and marker not in self.output[start:]:
                status_file = self.session / 'status.json'
                detail = json.loads(status_file.read_text()).get('error') if status_file.exists() else None
                raise AssertionError(f'Mirror owner exited: {detail or self.child.returncode}; '
                                     f'see {self.session}/owner.log and run-001/process.log')
        if marker not in self.output[start:]:
            raise AssertionError(f'Missing {marker!r}; see {self.root}/mirror.bin')

    def send(self, data):
        os.write(self.master, data)

    def wait(self):
        end = time.monotonic() + 12
        while self.child.poll() is None and time.monotonic() < end:
            self.drain()
        self.drain(.1)
        assert self.child.wait(timeout=1) == 0, self.session
        info = json.loads((self.session / 'status.json').read_text())
        assert info['state'] == 'finished', info
        log = (self.root / 'desktop-local.log').read_text(encoding='utf-8')
        assert 'Desktop stopped.' in log and ' | card=' in log, 'Adjacent runtime log incomplete'
        if info['generations'] > 1:
            assert 'Desktop reboot: starting a fresh process.' in log, 'Adjacent reboot log missing'
        assert not (self.card / LOCK_NAME).exists(), 'Card lock was not released'
        if self.master is not None:
            assert termios.tcgetattr(self.slave) == self.original, 'Terminal settings not restored'
        assert (self.card / 'A/0/PUN.TXT').read_bytes() == b'X', 'Guest storage not saved'
        assert (self.card / 'A/0/7XTGVTRM.BUF').stat().st_size > 0, 'History not saved'

    def close(self):
        if self.child.poll() is None:
            self.child.send_signal(signal.SIGTERM)
            try:
                self.child.wait(timeout=40)
            except subprocess.TimeoutExpired:
                self.child.kill()  # failed disposable fixture only
                self.child.wait()
        (self.root / 'mirror.bin').write_bytes(self.output)
        if self.master is not None:
            os.close(self.master)
        os.close(self.slave)


def run_case(root, build, target, mode):
    case = Case(root, build, target, mode)
    try:
        case.until(b'\x1b[?1049h')
        case.drain(.4)
        case.send(b'\x1b[>29v')
        case.drain(1.2)  # input is suppressed during the one-second profile notice
        case.send(b'VTHELLO\r')
        case.until(b'Mirror guest works')
        case.drain(.2)
        # A second cooperating owner must refuse this same card.
        try:
            with card_lock(case.card):
                raise AssertionError('Second owner acquired an active card')
        except RuntimeError as error:
            assert 'already locked' in str(error)
        if mode == 'reboot':
            case.send(b'VTREBOOT\r')
            end = time.monotonic() + 8
            while time.monotonic() < end:
                case.drain()
                info = json.loads((case.session / 'status.json').read_text())
                if info.get('generation') == 2 and info['state'] == 'running':
                    break
            else:
                raise AssertionError('Guest REBOOT did not restart')
            case.drain(.4)
            assert (case.card / 'A/0/PUN.TXT').read_bytes() == b'X'
            case.send(b'\x1dq')
        elif mode == 'exit':
            case.send(b'EXIT\r')
        elif mode in ('VTLOOP', 'VTFLOOD'):
            case.send(mode.encode() + b'\r')
            case.drain(.2)
            case.child.send_signal(signal.SIGTERM)
        elif mode == 'hangup':
            case.child.send_signal(signal.SIGHUP)
        elif mode == 'tty-close':
            os.close(case.master)
            case.master = None
        elif mode == 'transfer':
            payload = bytes(range(256)) * 2
            host = case.session / 'transfers/A/0'
            (host / 'VTSEND.BIN').write_bytes(payload)
            case.send(b'\x1b[911;6~')  # Mintty Ctrl+Shift+X, decoded before the shared panel
            case.until(b'VTPUT')
            case.send(b'sA0\rVTSEND.BIN\r\x15VTROUND.BIN\ry')
            case.until(b'Saved VTROUND.BIN', timeout=20)
            case.send(b'\r')
            case.drain(.3)
            case.send(b'\x1b[911;6~')  # Mintty Ctrl+Shift+X, decoded before the shared panel
            case.until(b'VTPUT')
            case.send(b'rA0\rVTROUND.BIN\r\x15VTRECV.BIN\r')
            case.until(b'Received', timeout=20)
            assert (host / 'VTRECV.BIN').read_bytes() == payload, 'Binary transfer mismatch'
            case.send(b'\r')
            case.drain(.3)
            case.send(b'\x1b[911;6~')  # Mintty Ctrl+Shift+X, decoded before the shared panel
            case.until(b'VTPUT')
            case.send(b'\x1b')  # cancel panel and reconstruct guest screen
            case.drain(.5)
            case.send(b'\x1dq')
        else:
            case.send(b'\x1dq')
        case.wait()
        print(f'PASS {mode}: real mirror, guest, cooperative save, card lock and TTY cleanup', flush=True)
    finally:
        case.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', default='ES3_5')
    parser.add_argument('--build-dir', type=Path)
    modes = ('quit', 'exit', 'reboot', 'VTLOOP', 'VTFLOOD', 'hangup', 'tty-close', 'transfer')
    parser.add_argument('--case', choices=modes)
    args = parser.parse_args()
    target = target_name(args.target)
    build = args.build_dir.resolve() if args.build_dir else OUTPUT / 'msys2-ucrt64' / target
    parent = build / 'mirror-proofs'
    parent.mkdir(exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='mirror-', dir=parent))
    print(f'Evidence: {root}', flush=True)
    class Tee:
        def __init__(self, stream, log): self.stream, self.log = stream, log
        def write(self, text):
            self.log.write(text); self.log.flush()
            return self.stream.write(text)
        def flush(self): self.stream.flush(); self.log.flush()
    with (root / 'proof.log').open('w', encoding='utf-8') as log, contextlib.redirect_stdout(Tee(sys.stdout, log)):
        try:
            for mode in ([args.case] if args.case else modes):
                run_case(root, build, target, mode)
        except BaseException as error:
            print(f'FAIL: {error}', flush=True)
            raise
        print('PASS: mirror proof complete.', flush=True)


if __name__ == '__main__':
    main()
