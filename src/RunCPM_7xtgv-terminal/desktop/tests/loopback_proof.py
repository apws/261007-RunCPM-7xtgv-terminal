"""Native/MSYS connection proof on fresh retained fixtures; also runs on Linux.

Uses the same loopback backend as Windows, without changing Linux PTY launch.
No live card, hardware connection, downloaded files or package installation.
"""
import argparse
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import pty
import queue
import secrets
import select
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import tty
import zipfile

SKETCH = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('windows_build', SKETCH/'tools-win/windows_build.py')
win = importlib.util.module_from_spec(spec)
spec.loader.exec_module(win)


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def mixed_wait():
    """MSYS must wait on terminal and socket descriptors in one event loop."""
    master, slave = pty.openpty()
    listener = socket.socket()
    client = peer = None
    try:
        tty.setraw(slave)
        listener.bind(('127.0.0.1', 0)); listener.listen(1)
        client = socket.create_connection(listener.getsockname(), timeout=5)
        peer, _ = listener.accept()
        os.write(master, b'K'); client.sendall(b'S')
        pending = {slave, peer.fileno()}
        until = time.monotonic() + 5
        while pending and time.monotonic() < until:
            ready, _, _ = select.select(list(pending), [], [], .2)
            for fd in ready:
                check(os.read(fd, 1) == (b'K' if fd == slave else b'S'), 'Mixed descriptor data mismatch')
                pending.remove(fd)
        check(not pending, 'Mixed PTY/socket select timed out')
        # The shared mirror/transfer code uses os.write/os.read on descriptors.
        check(os.write(peer.fileno(), b'R') == 1 and client.recv(1) == b'R', 'Socket descriptor write failed')
    finally:
        os.close(master); os.close(slave); listener.close()
        if client: client.close()
        if peer: peer.close()
    print('PASS: mixed terminal/socket select and descriptor I/O', flush=True)


class Session:
    def __init__(self, executable, args, directory, echo=False):
        self.token = secrets.token_hex(32)
        self.control = self.data = None
        self.lines = queue.Queue()
        self.log = (directory/'process.log').open('w', encoding='utf-8')
        command = [str(executable), *args, *([self.token] if echo else ['--loopback-token', self.token])]
        self.process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, encoding='utf-8', errors='replace')
        def read_output():
            for line in self.process.stdout:
                self.log.write(line); self.log.flush(); self.lines.put(line)
            self.lines.put(None)
        self.reader = threading.Thread(target=read_output, daemon=True)
        self.reader.start()

    def connect(self):
        until = time.monotonic() + 15
        while time.monotonic() < until:
            try: line = self.lines.get(timeout=.2)
            except queue.Empty: continue
            if line is None: raise RuntimeError('Child exited before publishing its endpoint')
            if line.startswith('LOOPBACK='):
                self.endpoint = json.loads(line[len('LOOPBACK='):]); break
        else: raise RuntimeError('No loopback endpoint within 15 seconds')
        # Invalid authentication must not acquire ownership or reach guest input.
        with socket.create_connection(('127.0.0.1', self.endpoint['control_port']), timeout=5) as wrong:
            wrong.sendall(b'XTGV/1 bad-token\n')
            try: rejected = wrong.recv(1) == b''
            except ConnectionResetError: rejected = True # rejected with unread bytes
            check(rejected, 'Invalid owner token was accepted')
        self.control = self.channel('control_port')
        self.data = self.channel('data_port')

    def channel(self, name):
        channel = socket.create_connection(('127.0.0.1', self.endpoint[name]), timeout=5)
        channel.sendall(('XTGV/1 ' + self.token + '\n').encode('ascii'))
        return channel

    def drain(self, seconds):
        data = bytearray(); until = time.monotonic() + seconds
        while time.monotonic() < until:
            if select.select([self.data], [], [], .05)[0]:
                chunk = self.data.recv(65536)
                if not chunk: break
                data.extend(chunk)
        return bytes(data)

    def wait(self, code=0):
        result = self.process.wait(timeout=8)
        self.reader.join(timeout=2)
        check(result == code, f'Child exit {result}, expected {code}; see {self.log.name}')

    def close(self):
        if self.process.poll() is None and not self.control and hasattr(self, 'endpoint'):
            try: self.control = self.channel('control_port')
            except OSError: pass
        if self.process.poll() is None and self.control:
            try: self.control.sendall(b'STOP\n')
            except OSError: pass
        if self.control: self.control.close()
        if self.data: self.data.close()
        try: self.process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            # Failed disposable test only; ordinary stop never uses forced kill.
            self.process.kill(); self.process.wait()
            print('FAIL: forced cleanup of stalled disposable fixture', flush=True)
        self.reader.join(timeout=2); self.process.stdout.close(); self.log.close()


def echo_proof(executable, directory):
    directory.mkdir()
    session = Session(executable, [], directory, echo=True)
    try:
        session.connect()
        # >both bounded queues; all byte values, including NUL and STOP text.
        payload = bytes(range(256))*1024 + b'STOP\n'
        session.data.setblocking(False)
        received = bytearray(); sent = 0; until = time.monotonic()+15
        while len(received) < len(payload) and time.monotonic() < until:
            readers, writers, _ = select.select([session.data], [session.data] if sent<len(payload) else [], [], .1)
            if writers:
                try: sent += session.data.send(payload[sent:sent+997])
                except BlockingIOError: pass
            if readers:
                try: chunk = session.data.recv(733)
                except BlockingIOError: continue
                check(bool(chunk), 'Echo disconnected early'); received.extend(chunk)
        check(received == payload, 'Binary loopback/backpressure mismatch')
        session.control.sendall(b'STOP\n'); session.wait()
    finally: session.close()
    print('PASS: 262149 exact bytes, fragmented I/O, bounded queues, separate STOP', flush=True)


def guest_proof(executable, directory, mode, visible):
    directory.mkdir(); area = directory/'A/0'; area.mkdir(parents=True)
    (area/'VTHELLO.COM').write_bytes(bytes.fromhex('1110010e09cd05001e580e04cd0500c9') + b'Windows guest works\r\n$')
    (area/'VTLOOP.COM').write_bytes(bytes.fromhex('c30001'))
    (area/'VTFLOOD.COM').write_bytes(bytes.fromhex('1e580e02cd0500c30001'))
    (area/'VTREBOOT.COM').write_bytes(bytes.fromhex('3e01d37cc9'))
    args = ['--card-root', win.native_path(directory), '--run-for-ms', '20000']
    if not visible: args.append('--headless')
    session = Session(executable, args, directory)
    try:
        session.connect()
        boot = session.drain(.8)
        check(bool(boot), 'No mirror boot/snapshot bytes')
        session.data.sendall(b'\x1b[>29v') # automation bank 9; wait for its transient notice
        session.drain(1.2)
        session.data.sendall(b'VTHELLO\r')
        until = time.monotonic()+5
        command_output = bytearray()
        while b'Windows guest works' not in command_output and time.monotonic()<until:
            command_output.extend(session.drain(.1))
        (directory/'command.bin').write_bytes(command_output)
        check(b'Windows guest works' in command_output, 'Guest program output missing')
        session.data.close(); time.sleep(.1)
        session.data = session.channel('data_port')
        session.data.sendall(b'\x1b[>62v')
        check(bool(session.drain(.5)), 'No mirror snapshot after reconnect')
        if mode in ('VTLOOP', 'VTFLOOD', 'VTREBOOT'):
            session.data.sendall(mode.encode()+b'\r'); time.sleep(.2)
        if mode == 'exit': session.data.sendall(b'EXIT\r')
        elif mode == 'disconnect': session.control.close(); session.control = None
        elif mode != 'VTREBOOT': session.control.sendall(b'STOP\n')
        session.wait(75 if mode == 'VTREBOOT' else 0)
        check((area/'PUN.TXT').read_bytes() == b'X', 'Guest Z80/BDOS storage proof missing after close')
        check((area/'7XTGVTRM.BUF').is_file(), 'History was not persisted on shutdown')
        (directory/'boot.bin').write_bytes(boot)
    finally: session.close()
    print(f'PASS: guest command/storage, reconnect, {mode} shutdown and history save', flush=True)


class Tee:
    def __init__(self, console, log): self.console, self.log = console, log
    def write(self, text):
        self.console.write(text); self.log.write(text)
        return len(text)
    def flush(self): self.console.flush(); self.log.flush()


def run_proof(build, evidence, suffix, headless):
    print(f'Python: {sys.version}; platform={sys.platform}; MSYSTEM={os.environ.get("MSYSTEM", "")}', flush=True)
    try:
        mixed_wait()
        storage_temp = evidence/'host-storage'; storage_temp.mkdir()
        environment = os.environ.copy()
        # Exercise the same filesystem as the selected proof cards, including
        # shared-drive rename/truncate semantics when launched from that checkout.
        for variable in ('TMPDIR', 'TEMP', 'TMP'):
            environment[variable] = win.native_path(storage_temp)
        for name in ('catalog', 'canvas', 'presentation', 'terminal_framebuffer', 'storage', 'storage_reuse'):
            result = subprocess.run([str(build/f'xtgv_{name}_test{suffix}')],
                                    env=environment, capture_output=True, text=True,
                                    errors='replace', timeout=20)
            print(result.stdout + result.stderr, end='', flush=True)
            check(result.returncode == 0, f'{name} test failed: {result.returncode}')
        echo_proof(build/f'xtgv_loopback_echo{suffix}', evidence/'binary')
        for mode in ('idle', 'VTLOOP', 'VTFLOOD', 'disconnect', 'exit', 'VTREBOOT'):
            guest_proof(build/f'xtgv_desktop{suffix}', evidence/mode, mode, mode=='idle' and not headless)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'FAIL: {error}\nEvidence retained: {evidence}', flush=True)
        return 1
    print(f'PASS: connection proof complete. Evidence retained: {evidence}', flush=True)
    return 0


def run_smoke(build, evidence, suffix):
    card = evidence/'smoke'; (card/'A/0').mkdir(parents=True)
    print(f'Ten-second visual smoke test; disposable card: {win.native_path(card)}', flush=True)
    try:
        result = subprocess.run([str(build/f'xtgv_desktop{suffix}'),
                                 '--card-root', win.native_path(card), '--run-for-ms', '10000'],
                                capture_output=True, text=True, errors='replace', timeout=25)
        print(result.stdout + result.stderr, end='', flush=True)
        print(f'Test exit code: {result.returncode}', flush=True)
        return result.returncode
    except (OSError, subprocess.SubprocessError) as error:
        print(f'FAIL: {error}', flush=True)
        return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', default='ES3_5', type=win.target_name)
    parser.add_argument('--build-dir')
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--smoke-only', action='store_true', help='Only run the ten-second visual boot check')
    parser.add_argument('--report-dir', help='Also copy the report ZIP here, e.g. a shared folder')
    parser.add_argument('--fixture-dir', help='Parent for fresh test cards; default: build output/proof-cards')
    args = parser.parse_args()
    if not args.build_dir: win.require_msys()
    build = win.host_path(args.build_dir) if args.build_dir else win.OUTPUT/'msys2-ucrt64'/args.target
    suffix = '.exe' if sys.platform == 'cygwin' else ''
    fixture_parent = win.host_path(args.fixture_dir) if args.fixture_dir else build/'proof-cards'
    fixture_parent.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix='xtgv-loopback-proof-', dir=fixture_parent))
    print(f'Evidence and disposable cards: {evidence}', flush=True)
    with (evidence/'proof.log').open('w', encoding='utf-8') as log:
        with contextlib.redirect_stdout(Tee(sys.stdout, log)):
            result = (run_smoke(build, evidence, suffix) if args.smoke_only else
                      run_proof(build, evidence, suffix, args.headless))
    reports = build/'proof-reports'; reports.mkdir(exist_ok=True)
    archive = reports/(evidence.name+'.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as package:
        for path in sorted(evidence.rglob('*')):
            if path.is_file(): package.write(path, path.relative_to(evidence))
    print(f'Report ZIP: {win.native_path(archive)}', flush=True)
    if args.report_dir:
        destination = win.host_path(args.report_dir)
        try:
            destination.mkdir(parents=True, exist_ok=True)
            if destination.resolve() != reports.resolve():
                shutil.copy2(archive, destination/archive.name)
            print(f'Shared report: {win.native_path(destination/archive.name)}', flush=True)
        except OSError as error:
            print(f'Cannot copy shared report: {error}; local report retained at {archive}', flush=True)
            return 1
    return result


if __name__ == '__main__':
    raise SystemExit(main())
