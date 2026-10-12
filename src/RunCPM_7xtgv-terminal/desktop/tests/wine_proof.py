#!/usr/bin/env python3
"""Run existing mirror acceptance cases against a Windows binary through Wine.

Linux proof only. Retains fresh cards, transcripts and snapshots under --evidence.
Requires an initialized isolated WINEPREFIX; never installs or rebuilds anything.
"""
import argparse
import fcntl
import os
from pathlib import Path
import pty
import shutil
import struct
import subprocess
import sys
import termios
from unittest.mock import patch

import mirror_loopback as proof


class WineCase(proof.Case):
    def __init__(self, root, build, target, mode):
        self.root = root / mode
        self.card = self.root / 'card with spaces žluťoučký'
        area = self.card / 'A/0'
        area.mkdir(parents=True, exist_ok=False)
        (area / '7XTGVTRM.INI').write_bytes(
            b'[display:ES3_35]\r\nview_zoom=1\r\nzoom1_terminal=80x24\r\n'
            b'[global]\r\nccp_bank=0\r\n[bank0]\r\ninput=ansi\r\n'
            b'[bank9]\r\ninput=ansi\r\n\x1a')
        (area / 'VTHELLO.COM').write_bytes(
            bytes.fromhex('1110010e09cd05001e580e04cd0500c9') + b'Mirror guest works\r\n$')
        (area / 'VTLOOP.COM').write_bytes(bytes.fromhex('c30001'))
        (area / 'VTFLOOD.COM').write_bytes(bytes.fromhex('1e580e02cd0500c30001'))
        (area / 'VTREBOOT.COM').write_bytes(bytes.fromhex('3e01d37cc9'))
        for name in ('VTPUT.COM', 'VTGET.COM'):
            shutil.copyfile(proof.REPO / 'cpmcard/A/0' / name, area / name)
        self.session = self.root / 'session'
        # Existing acceptance assertions expect the adjacent launcher transcript.
        (self.root / 'desktop-local.log').symlink_to('session/owner.log')
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack('HHHH', 25, 80, 0, 0))
        self.original = termios.tcgetattr(self.slave)
        self.child = subprocess.Popen(
            [sys.executable, str(Path(__file__).with_name('wine_owner.py')),
             '--binary', str(build / 'xtgv_desktop.exe'), '--card-root', str(self.card),
             '--session-dir', str(self.session), '--headless'],
            stdin=self.slave, stdout=self.slave, stderr=self.slave)
        self.output = bytearray()

    def send(self, data):
        # Invoke the same transfer panel using its portable prefix, rather than
        # the Mintty-specific physical-key encoding used by the Windows test.
        if data == b'\x1b[911;6~':
            data = b'\x1dm'
        super().send(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--case', choices=('quit', 'exit', 'reboot', 'VTLOOP',
                                         'VTFLOOD', 'hangup', 'tty-close', 'transfer'))
    args = parser.parse_args()
    root = args.evidence.resolve()
    root.mkdir(parents=True, exist_ok=False)
    modes = [args.case] if args.case else (
        'quit', 'exit', 'reboot', 'VTLOOP', 'VTFLOOD', 'hangup', 'tty-close', 'transfer')
    with patch.object(proof, 'Case', WineCase):
        for mode in modes:
            proof.run_case(root, args.build_dir.resolve(), 'jc8048w535', mode)
    print('PASS: Wine ES3_35 proof complete; native POSIX mirror, unmodified Windows binary.')


if __name__ == '__main__':
    main()
