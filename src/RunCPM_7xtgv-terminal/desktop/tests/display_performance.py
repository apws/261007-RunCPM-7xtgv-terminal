"""Compare SDL backends and headless terminal output on disposable cards."""
import argparse
import json
import os
from pathlib import Path
import select
import sys
import tempfile
import time

from loopback_proof import Session, win


def wait_text(session, marker, timeout, chunks=None):
    received = bytearray()
    started = time.monotonic()
    until = started + timeout
    while marker not in received and time.monotonic() < until:
        if select.select([session.data], [], [], .05)[0]:
            chunk = session.data.recv(65536)
            if not chunk:
                break
            received.extend(chunk)
            if chunks is not None:
                chunks.append(dict(seconds=round(time.monotonic() - started, 6), bytes=len(chunk)))
    if marker not in received:
        raise RuntimeError(f'Timed out waiting for {marker!r}')
    return received


def measure(build, root, target, mode):
    directory = root / mode
    area = directory / 'A/0'
    area.mkdir(parents=True)
    display = win.TARGETS[target]['display_id']
    (area / '7XTGVTRM.INI').write_bytes(
        (f'[display:{display}]\r\nview_zoom=2\r\nzoom2_terminal=80x24\r\n'
         'render_flush_hz=10\r\n[global]\r\nccp_bank=9\r\n[bank9]\r\ninput=ansi\r\n').encode() + b'\x1a')
    # One BDOS-9 call prints enough lines to exercise scrolling and backpressure.
    payload = (b'VT display throughput: 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n' * 200
               + b'VT PERFORMANCE DONE\r\n$')
    (area / 'VTPERF.COM').write_bytes(bytes.fromhex('1109010e09cd0500c9') + payload)
    args = ['--card-root', win.native_path(directory), '--timing-report']
    if mode == 'coarse-timer':
        args.append('--coarse-timer')
    if mode == 'headless':
        args.append('--headless')
    previous = os.environ.pop('SDL_RENDER_DRIVER', None)
    if mode == 'software':
        os.environ['SDL_RENDER_DRIVER'] = 'software'
    session = None
    try:
        start = time.monotonic()
        suffix = '.exe' if sys.platform == 'cygwin' else ''
        session = Session(build / ('xtgv_desktop' + suffix), args, directory)
        session.connect()
        boot = wait_text(session, b'A0>', 30)
        boot_seconds = time.monotonic() - start
        session.drain(.3)
        start = time.monotonic()
        session.data.sendall(b'VTPERF\r')
        chunks = []
        output = wait_text(session, b'VT PERFORMANCE DONE', 45, chunks)
        (directory / 'mirror-chunks.json').write_text(json.dumps(chunks, indent=2) + '\n')
        scroll_seconds = time.monotonic() - start
        # Mirror delivery can finish before the panel repaints. Keep the native
        # loop running long enough to observe the reported eight-second redraw.
        settle_seconds = .3 if os.environ.get('SDL_VIDEODRIVER') == 'dummy' else 10
        print(f'{mode}: mirror received final text; observing redraw for {settle_seconds:g}s ...', flush=True)
        output.extend(session.drain(settle_seconds))
        session.control.sendall(b'STOP\n')
        session.wait()
        (directory / 'boot.bin').write_bytes(boot)
        (directory / 'scroll.bin').write_bytes(output)
        result = dict(mode=mode, boot_seconds=round(boot_seconds, 3),
                      mirror_output_seconds=round(scroll_seconds, 3),
                      settle_seconds=settle_seconds, mirror_bytes=len(output))
        print(json.dumps(result), flush=True)
        return result
    finally:
        if session:
            session.close()
        os.environ.pop('SDL_RENDER_DRIVER', None)
        if previous is not None:
            os.environ['SDL_RENDER_DRIVER'] = previous


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', default='ES3_5')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--dummy-video', action='store_true', help='Offscreen Linux harness check only')
    parser.add_argument('--storage-compare', action='store_true', help='Compare identical cards on build storage and local TEMP')
    args = parser.parse_args()
    if args.dummy_video:
        os.environ['SDL_VIDEODRIVER'] = 'dummy'
    target = win.target_name(args.target)
    build = args.build_dir.resolve() if args.build_dir else win.OUTPUT / 'msys2-ucrt64' / target
    if args.storage_compare:
        from storage_performance import compare
        compare(build, target)
        return
    parent = build / 'performance-proofs'
    parent.mkdir(exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='display-', dir=parent))
    print(f'Evidence: {root}', flush=True)
    results = []
    try:
        for mode in ('coarse-timer', 'default', 'software', 'headless'):
            results.append(measure(build, root, target, mode))
    finally:
        (root / 'timings.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
