#!/usr/bin/env python3
"""Experimental native POSIX mirror owner for an existing Windows EXE under Wine.

Use an initialized, isolated WINEPREFIX and a disposable card. This is a proof
entry point, not a supported target launcher or a Wine/MSYS2 installer.
"""
import argparse
import errno
import os
from pathlib import Path
import queue
import secrets
import signal
import subprocess
import sys
import threading

SKETCH = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SKETCH / 'tools-win'))
from windows_run import DesktopProcess, status
from desktop_card_lock import card_lock
from shortcut_log import append_log


def wine_path(path):
    return subprocess.check_output(['winepath', '-w', str(path.resolve())],
                                   text=True, timeout=15).strip()


class WineProcess(DesktopProcess):
    # Reuse the established authenticated endpoint reader and cooperative STOP.
    # Only process creation/path conversion differ from the Windows owner.
    def __init__(self, binary, card, directory, headless, scale, launcher_log):
        self.launcher_log = launcher_log
        self.token = secrets.token_hex(32)
        self.control = None
        self.lines = queue.Queue()
        command = ['wine', str(binary), '--card-root', wine_path(card),
                   '--loopback-token', self.token, '--window-scale', str(scale),
                   '--snapshot', wine_path(directory / 'frame.ppm'), '--timing-report']
        if headless:
            command.append('--headless')
        self.log = (directory / 'process.log').open('wb')
        try:
            self.child = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                          start_new_session=True)
        except BaseException:
            self.log.close()
            raise
        self.reader = threading.Thread(target=self.read_output, daemon=True)
        self.reader.start()


def serve(args):
    directory = args.session_dir.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    card = args.card_root.resolve(strict=True)
    binary = args.binary.resolve(strict=True)
    log = directory / 'owner.log'
    transfer = directory / 'transfers'
    for drive in 'ABCDEFGHIJKLMNOP':
        for area in '0123456789ABCDEF':
            if (card / drive / area).is_dir():
                (transfer / drive / area).mkdir(parents=True, exist_ok=True)
    os.environ['XTGV_TRANSFER_ROOT'] = str(transfer)
    os.environ.pop('XTGV_REFLASH_COMMAND', None)
    from terminal import run
    closing = cleanup = False

    def interrupted(signum, frame):
        nonlocal closing
        closing = True
        if not cleanup:
            raise KeyboardInterrupt

    signals = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)
    previous = {s: signal.signal(s, interrupted) for s in signals}
    try:
        with card_lock(card) as lease:
            generation = 0
            while not closing:
                generation += 1
                current = directory / f'run-{generation:03d}'
                current.mkdir()
                process = None
                reason = 'closed'
                try:
                    process = WineProcess(binary, card, current, args.headless,
                                          args.window_scale, log)
                    endpoint = process.connect()
                    status(directory, 'running', generation=generation,
                           pid=process.child.pid, card=str(card))
                    # Native Konsole/Kitty events: no Mintty decoder or hook.
                    reason = run(endpoint, None,
                                 should_stop=lambda: process.child.poll() is not None)
                except KeyboardInterrupt:
                    closing = True
                except OSError as error:
                    if error.errno in (errno.EIO, errno.ENXIO) and not os.isatty(0):
                        closing = True
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
                    append_log(log, 'Desktop reboot: starting a fresh process.\n')
                    status(directory, 'restarting', generation=generation)
                    continue
                if code not in (0, 75):
                    raise RuntimeError(f'Wine desktop exited with code {code}; see {current}')
                break
        append_log(log, 'Desktop stopped.\n')
        status(directory, 'finished', generations=generation, card=str(card))
    except BaseException as error:
        status(directory, 'failed', error=str(error), card=str(card))
        raise
    finally:
        for s, handler in previous.items():
            signal.signal(s, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--card-root', required=True, type=Path)
    parser.add_argument('--session-dir', required=True, type=Path)
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--window-scale', type=int, choices=range(1, 10), default=2)
    args = parser.parse_args()
    prefix = os.environ.get('WINEPREFIX')
    if not prefix or not (Path(prefix) / 'system.reg').is_file():
        parser.error('Set WINEPREFIX to an explicitly initialized test prefix first.')
    serve(args)


if __name__ == '__main__':
    main()
