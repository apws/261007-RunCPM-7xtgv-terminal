#!/usr/bin/env python3
"""Use the ordinary terminal mirror against this proof's loopback bridge."""
import argparse
import json
import os
from pathlib import Path
import signal
import sys
import urllib.request

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'tools'))
from desktop_connection import LoopbackEndpoint
from terminal import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    args = parser.parse_args()
    info = json.loads((args.session / 'session.json').read_text())
    # Transfers are isolated from the source card as well.
    transfer = args.session / 'transfers'
    for drive in 'ABCDEFGHIJKLMNOP':
        for area in '0123456789ABCDEF':
            (transfer / drive / area).mkdir(parents=True, exist_ok=True)
    os.environ['XTGV_TRANSFER_ROOT'] = str(transfer)
    os.environ.pop('XTGV_REFLASH_COMMAND', None)
    closing = False

    def stop(signum, frame):
        nonlocal closing
        closing = True

    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, stop)
    def finished():
        if closing:
            return True
        try:
            with urllib.request.urlopen(info['url'] + 'status', timeout=1) as response:
                return json.load(response)['status'] in ('finished', 'failed')
        except OSError:
            return True
    # Avoid one HTTP poll per keyboard iteration.
    import time
    last = 0.0
    def poll():
        nonlocal last
        if closing:
            return True
        now = time.monotonic()
        if now - last < .25:
            return False
        last = now
        return finished()
    run(LoopbackEndpoint(info['port'], info['token']), None, should_stop=poll)


if __name__ == '__main__':
    main()
