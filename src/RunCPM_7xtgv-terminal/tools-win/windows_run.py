#!/usr/bin/env python3
"""Mintty owner for the shared mirror and native Windows desktop process."""
import argparse
import contextlib
import errno
import json
import os
from pathlib import Path
import queue
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import traceback

from windows_build import OUTPUT, REPO, SKETCH, TARGETS, host_path, native_path, require_msys, target_name
from mintty_profile import write_profile
from mintty_keys import MinttyInput
from key_bridge import KeyBridge, mintty_environment
from shortcut_log import shortcut_log, append_log
from desktop_card_lock import LOCK_NAME, card_lock
from desktop_connection import LoopbackEndpoint


def status(directory, state, **details):
    temporary = directory / 'status.tmp'
    temporary.write_text(json.dumps({'state': state, **details}, ensure_ascii=False) + '\n', encoding='utf-8')
    temporary.replace(directory / 'status.json')


def executable(build):
    return build / ('xtgv_desktop.exe' if sys.platform == 'cygwin' else 'xtgv_desktop')


class DesktopProcess:
    def __init__(self, binary, card, directory, headless, window_scale=1, launcher_log=None):
        self.launcher_log = launcher_log
        self.token = secrets.token_hex(32)
        self.control = None
        self.lines = queue.Queue()
        self.log = (directory / 'process.log').open('wb')
        command = [str(binary), '--card-root', native_path(card), '--loopback-token', self.token]
        if window_scale != 1:
            command += ['--window-scale', str(window_scale)]
        if headless:
            command.append('--headless')
        try:
            # A separate process group prevents terminal hangup from bypassing
            # the native cooperative STOP path. No terminal input is inherited.
            self.child = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                          start_new_session=True)
        except BaseException:
            self.log.close()
            raise
        self.reader = threading.Thread(target=self.read_output, daemon=True)
        self.reader.start()

    def read_output(self):
        try:
            for line in self.child.stdout:
                self.log.write(line)
                self.log.flush()
                append_log(self.launcher_log, line.decode("utf-8", errors="replace"))
                if line.startswith(b'LOOPBACK='):
                    self.lines.put(line)
        finally:
            self.child.stdout.close()
            self.log.close()
            self.lines.put(None)

    def connect(self):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                line = self.lines.get(timeout=.1)
            except queue.Empty:
                continue
            if line is None:
                raise RuntimeError('Desktop exited before announcing its connection; see process.log.')
            try:
                ports = json.loads(line.split(b'=', 1)[1])
            except (ValueError, IndexError):
                raise RuntimeError('Invalid desktop startup response; see process.log.') from None
            endpoint = LoopbackEndpoint(ports['data_port'], self.token)
            # Validate both endpoint fields before connecting to the owner.
            LoopbackEndpoint(ports['control_port'], self.token)
            self.control = socket.create_connection(('127.0.0.1', ports['control_port']), timeout=5)
            self.control.sendall(('XTGV/1 ' + self.token + '\n').encode('ascii'))
            return endpoint
        raise RuntimeError('No desktop connection within 15 seconds; see process.log.')

    def stop(self):
        if self.control is not None:
            with contextlib.suppress(OSError):
                self.control.sendall(b'STOP\n')
            self.control.close()
            self.control = None
        # Before owner authentication the backend has a 30-second startup
        # watchdog. After authentication, STOP/EOF requests immediate cleanup.
        try:
            result = self.child.wait(timeout=35)
        except subprocess.TimeoutExpired:
            raise RuntimeError(f'Desktop PID {self.child.pid} did not stop. Card lock retained; '
                               'inspect process.log before recovery. No forced kill was used.') from None
        self.reader.join(timeout=2)
        return result


def serve(args, build, directory, card):
    # Set before importing terminal: its transfer panel captures this at import.
    transfer = directory / 'transfers'
    transfer.mkdir(exist_ok=True)
    for drive in 'ABCDEFGHIJKLMNOP':
        for area in '0123456789ABCDEF':
            if (card / drive / area).is_dir():
                (transfer / drive / area).mkdir(parents=True, exist_ok=True)
    os.environ['XTGV_TRANSFER_ROOT'] = str(transfer)
    os.environ.pop('XTGV_REFLASH_COMMAND', None)
    from terminal import run
    closing = False
    cleanup = False

    def interrupted(signum, frame):
        nonlocal closing
        closing = True
        if not cleanup:
            raise KeyboardInterrupt

    signals = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)
    saved = {s: signal.signal(s, interrupted) for s in signals}
    try:
        with KeyBridge(build, directory / 'mintty.conf'), card_lock(card) as lease:
            generation = 0
            while not closing:
                generation += 1
                current = directory / f'run-{generation:03d}'
                current.mkdir()
                process = None
                reason = 'closed'
                try:
                    process = DesktopProcess(executable(build), card, current, args.headless, args.window_scale, args.log)
                    endpoint = process.connect()
                    status(directory, 'running', generation=generation, pid=process.child.pid,
                           card=str(card), transfer_root=str(transfer))
                    reason = run(endpoint, None, should_stop=lambda: process.child.poll() is not None,
                                 input_decoder=MinttyInput())
                except KeyboardInterrupt:
                    closing = True
                except OSError as error:
                    if error.errno in (errno.EIO, errno.ENXIO) and not os.isatty(0):
                        closing = True  # terminal disappeared before delivering SIGHUP
                    else:
                        raise
                finally:
                    cleanup = True
                    if process is not None:
                        try:
                            code = process.stop()
                        except BaseException:
                            if process.child.poll() is None:
                                lease.retain()
                            raise
                    cleanup = False
                if process is None:
                    break
                if code == 75 and reason == 'stopped' and not closing:
                    append_log(args.log, 'Desktop reboot: starting a fresh process.\n')
                    status(directory, 'restarting', generation=generation)
                    continue
                if code not in (0, 75):
                    raise RuntimeError(f'Desktop exited with code {code}; see {current / "process.log"}')
                break
        append_log(args.log, 'Desktop stopped.\n')
        status(directory, 'finished', generations=generation, card=str(card))
        return 0
    finally:
        for s, handler in saved.items():
            signal.signal(s, handler)


def launch(args, target, build):
    require_msys()
    environment = mintty_environment()
    mintty = Path('/usr/bin/mintty.exe')
    if not mintty.is_file():
        raise RuntimeError('Mintty is missing. Run tools-win/setup.cmd first.')
    print(f'Desktop module: {TARGETS[target]["display_id"]} ({target})', flush=True)
    print(f'SDL window scale: {args.window_scale}x (fixed size)', flush=True)
    parent = build / 'sessions'
    parent.mkdir(exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix=time.strftime('%y%m%d-%H%M-'), dir=parent))
    source = host_path(args.card_root) if args.card_root else REPO / 'cpmcard'
    if not (source / 'A/0').is_dir():
        raise RuntimeError(f'Card needs an A/0 directory: {source}')
    card = source
    print('Card: ' + ('local folder' if args.card_mode == 'local' else
                      'selected folder' if args.card_mode == 'card' else 'disposable copy') +
          ' (read/write, including INI/history)', flush=True)
    if args.card_mode == 'test':
        card = directory / 'card'
        # Copy the complete card; exclude only our temporary coordination lock.
        with card_lock(source):
            shutil.copytree(source, card, ignore=lambda path, names:
                            [LOCK_NAME] if Path(path) == source else [])
    profile = directory / 'mintty.conf'
    write_profile(profile)
    status(directory, 'prepared', card=str(card))
    command = [str(mintty), '--config', str(profile), '--hold', 'error',
               '--title', f'7xtgv-terminal {TARGETS[target]["display_id"]} ({args.card_mode})',
               '--exec', '/usr/bin/python3', str(Path(__file__).resolve()), 'serve',
               '--target', target, '--card-root', str(card), '--session-dir', str(directory),
               '--window-scale', str(args.window_scale)]
    if args.log: command += ['--log', str(args.log)]
    append_log(args.log, '\n*** MIRROR OWNER ***\n' + repr(command) + '\n')
    print('Launching desktop and Mintty mirror ...', flush=True)
    with (directory / 'mintty.log').open('wb') as log:
        subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log,
                         stderr=subprocess.STDOUT, start_new_session=True, env=environment)
    details = f'Card: {native_path(card)}\nSession logs: {native_path(directory)}\n'
    if args.log:
        append_log(args.log, details)
    else:
        print(details, end='', flush=True)
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        info = json.loads((directory / 'status.json').read_text(encoding='utf-8'))
        if info['state'] in ('running', 'restarting', 'finished'):
            process_log = directory / 'run-001/process.log'
            if process_log.exists():
                for line in process_log.read_text(encoding='utf-8', errors='replace').splitlines():
                    if line.startswith('Desktop ') and ' | ' in line:
                        print('Embedded build: ' + line.split(' | ')[1], flush=True)
                        break
            print('Desktop stopped.' if info['state'] == 'finished' else 'Desktop and mirror: running.', flush=True)
            return 0
        if info['state'] == 'failed':
            raise RuntimeError(info['error'])
        time.sleep(.1)
    hint = args.log.name if args.log else str(directory / 'mintty.log')
    raise RuntimeError(f'Mirror startup not confirmed. See {hint}.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('launch', 'serve'))
    parser.add_argument('--target', default='ES3_5')
    parser.add_argument('--card-mode', choices=('test', 'local', 'card'), default='test')
    parser.add_argument('--window-scale', type=int, choices=range(1, 10), default=1,
                        help='Fixed SDL window integer scale (1..9); guest geometry is unchanged')
    parser.add_argument('--log', default=os.environ.get('XTGV_SHORTCUT_LOG'))
    parser.add_argument('--card-root')
    parser.add_argument('--session-dir', type=Path)
    parser.add_argument('--build-dir', type=Path, help='Explicit build for host lifecycle tests')
    parser.add_argument('--headless', action='store_true', help='Host lifecycle tests only')
    args = parser.parse_args()
    target = target_name(args.target)
    build = args.build_dir.resolve() if args.build_dir else OUTPUT / 'msys2-ucrt64' / target
    directory = args.session_dir
    args.log = host_path(args.log) if args.log else None
    try:
        if not executable(build).is_file():
            append_log(args.log, f'Missing executable: {executable(build)}\n')
            raise RuntimeError('Desktop executable missing; run desktop-rebuild.cmd first.')
        if args.action == 'launch':
            if args.card_mode == 'card' and not args.card_root:
                raise RuntimeError('desktop-card.cmd requires --card-root "DRIVE:\\path\\to\\card".')
            if args.card_mode == 'local' and args.card_root:
                raise RuntimeError('Use desktop-card.cmd for an explicit card root.')
            with shortcut_log(args.log):
                if args.log: print(f'Log: {args.log.name}', flush=True)
                return launch(args, target, build)
        if not directory or not args.card_root:
            parser.error('serve requires --session-dir and --card-root')
        directory = host_path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        with (directory / 'owner.log').open('a', encoding='utf-8') as log:
            log.write(f'Start {time.strftime("%F %T")} target={target} card={args.card_root}\n')
        return serve(args, build, directory, host_path(args.card_root))
    except BaseException as error:
        if directory and directory.is_dir():
            with (directory / 'owner.log').open('a', encoding='utf-8') as log:
                traceback.print_exc(file=log)
            status(directory, 'failed', error=str(error))
        with contextlib.suppress(OSError):
            append_log(args.log, f'FAILED: {error}\n')
            print(f'Windows desktop: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
