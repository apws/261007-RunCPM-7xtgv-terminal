"""Compare identical disposable guest cards on the build share and local TEMP.

No working-card access. Uses the existing native executable without rebuilding.
DIR includes a final tiny marker program; a separate baseline measures that cost.
"""
import json
import os
from pathlib import Path
import sys
import tempfile
import time

from loopback_proof import Session, win
from display_performance import wait_text


def print_program(payload):
    return bytes.fromhex('1109010e09cd0500c9') + payload + b'$'


def prepare(card, target):
    area = card / 'A/0'
    area.mkdir(parents=True)
    ini = (f'[display:{win.TARGETS[target]["display_id"]}]\r\nview_zoom=2\r\n'
           'zoom2_terminal=80x24\r\nrender_flush_hz=10\r\n'
           '[global]\r\nccp_bank=9\r\n[bank9]\r\ninput=ansi\r\n')
    (area / '7XTGVTRM.INI').write_bytes(ini.encode() + b'\x1a')
    for n in range(128):
        (area / f'VT{n:06d}.DAT').write_bytes(bytes(range(128)))
    for trial in range(3):
        for kind in ('S', 'D', 'P'):
            marker = f'VT DONE {trial} {kind}\r\n'.encode()
            payload = (b'VT memory output 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n' * 200
                       if kind == 'P' else b'')
            (area / f'VT{kind}{trial}.COM').write_bytes(print_program(payload + marker))


def measure(build, evidence, card, target):
    evidence.mkdir()
    prepare(card, target)
    suffix = '.exe' if sys.platform == 'cygwin' else ''
    session = Session(build / ('xtgv_desktop' + suffix),
                      ['--card-root', win.native_path(card), '--headless', '--timing-report'], evidence)
    results = []
    try:
        session.connect()
        wait_text(session, b'A0>', 45)
        session.drain(.3)
        for trial in range(3):
            # Unique output markers prevent retained-screen redraws from
            # satisfying a later measurement with an earlier result.
            for name, kind in (('stamp', 'S'), ('dir_and_stamp', 'D'), ('text_output', 'P')):
                command = (b'DIR\r' if kind == 'D' else b'') + f'VT{kind}{trial}\r'.encode()
                marker = f'VT DONE {trial} {kind}'.encode()
                chunks = []
                start = time.monotonic()
                session.data.sendall(command)
                data = wait_text(session, marker, 90, chunks)
                elapsed = time.monotonic() - start
                data.extend(session.drain(.25))
                prefix = evidence / f'{trial}-{name}'
                prefix.with_suffix('.bin').write_bytes(data)
                prefix.with_suffix('.json').write_text(json.dumps(chunks, indent=2) + '\n')
                result = dict(trial=trial, operation=name, seconds=round(elapsed, 4), bytes=len(data))
                results.append(result)
                (evidence / 'timings.json').write_text(json.dumps(results, indent=2) + '\n')
                print(evidence.name, result, flush=True)
        session.control.sendall(b'STOP\n')
        session.wait()
        return dict(card=str(card), timings=results)
    finally:
        session.close()


def compare(build, target):
    parent = build / 'storage-proofs'
    parent.mkdir(exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='storage-', dir=parent))
    # Under MSYS, explicitly convert native TEMP, rather than assuming /tmp
    # refers to the same drive as the project/share.
    temporary = win.host_path(os.environ.get('TEMP', tempfile.gettempdir()))
    local = Path(tempfile.mkdtemp(prefix='xtgv-storage-', dir=temporary)) / 'card'
    print('Storage evidence:', root, '\nLocal test card:', local, flush=True)
    results = {}
    try:
        for name, card in (('shared', root / 'shared-card'), ('local-temp', local)):
            results[name] = measure(build, root / name, card, target)
    finally:
        (root / 'comparison.json').write_text(json.dumps(results, indent=2) + '\n')
    print('PASS: storage comparison complete; both disposable cards retained.', flush=True)
