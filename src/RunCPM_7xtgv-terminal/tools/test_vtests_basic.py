#!/usr/bin/env python3
"""Validate fetched MBASIC ASCII sources and VTESTS output in local RunCPM.

Checks guest output, not the embedded terminal renderer. No serial/device I/O.
"""
import os
from pathlib import Path
import pty
import re
import shutil
import subprocess
import tempfile

from build_vtpal import ROOT, receive

SOURCE = ROOT / "cpmdrive/A7 - MBASIC and VT tests"
FILES = ("VTESTS", "PROG", "VTPAL", "VTDEMO", "VTDEMO2")
ROLES = ("implicit", "red", "green", "blue", "shade", "cyan", "magenta", "yellow")
SGR = (30, 31, 32, 34, 37, 36, 35, 33)


def check_compact_page(output):
    """Check emitted text positions, including headings and pause prompt."""
    row, col = 1, 1
    for token in re.findall(rb'\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][B0]|[^\x1b]', output):
        if token.startswith((b'\x1b(', b'\x1b)')):
            continue
        if token.startswith(b'\x1b['):
            params, command = token[2:-1], token[-1:]
            if command == b'H':
                coords = params.split(b';')
                row = int(coords[0] or b'1')
                col = int(coords[1] or b'1') if len(coords) > 1 else 1
                assert 1 <= row <= 16 and 1 <= col <= 40, token
            else:
                assert command in (b'm', b'J'), token
        elif token == b'\r':
            col = 1
        elif token == b'\n':
            row += 1
        elif token[0] >= 32:
            assert 1 <= row <= 16 and 1 <= col <= 40, (row, col, token)
            col += 1


def main():
    for name in FILES:
        data = (SOURCE / (name + ".BAS")).read_bytes()
        body, eof, _ = data.partition(b"\x1a")
        assert eof, name
        assert b"\n" not in body.replace(b"\r\n", b""), name
        lines = body.decode("ascii").splitlines()
        if name == "VTESTS":
            assert all(len(line) <= 80 for line in lines), name
        assert all(re.match(r"^\d+ ", line) for line in lines if line.strip()), name
    with tempfile.TemporaryDirectory(prefix="xtgv-vtests-") as temporary:
        drive = Path(temporary) / "A/0"
        drive.mkdir(parents=True)
        for name in ("MBASIC.COM",) + tuple(n + ".BAS" for n in FILES):
            shutil.copyfile(SOURCE / name, drive / name)
        master, slave = pty.openpty()
        process = subprocess.Popen([str(ROOT / "build/RunCPM_linux/RunCPM")],
            cwd=temporary, stdin=slave, stdout=slave, stderr=slave,
            env=dict(os.environ, TERM="xterm"), start_new_session=True)
        os.close(slave)
        try:
            receive(master)
            os.write(master, b"MBASIC\r")
            receive(master, b"OK\r")
            for name in FILES:
                os.write(master, f'LOAD "{name}"\r'.encode())
                result = receive(master, b"OK\r")
                assert b"error" not in result.lower(), result
            for command in ('LOAD "VTESTS"', 'SAVE "VTASCII",A',
                            'NEW', 'LOAD "VTASCII"'):
                os.write(master, (command + "\r").encode())
                result = receive(master, b"OK\r")
                assert b"error" not in result.lower(), result
            assert (drive / "VTASCII.BAS").read_bytes().startswith(b"10 ")
            os.write(master, b"RUN\r")
            receive(master, b"Select: ")
            for choice in range(1, 9):
                os.write(master, f"{choice}\r".encode())
                prompt = (b"Enter: all 32 glyphs " if choice == 4
                          else b"Enter returns to menu: ")
                result = receive(master, prompt)
                assert b"error" not in result.lower(), result
                if choice == 1:
                    previous = -1
                    for index, (role, sgr) in enumerate(zip(ROLES, SGR)):
                        row = 3 + index
                        normal = (f"\x1b[{row};1H\x1b[{sgr}m"
                                  f"{index:2} {role[0]} {role} ({sgr})").encode()
                        alt = (f"\x1b[{row};19H\x1b[{sgr+60}m"
                               f"{index+8:2} {role[0].upper()} ALT "
                               f"{role.upper()} ({sgr+60})").encode()
                        position = result.index(normal)
                        assert position > previous
                        previous = result.index(alt)
                        assert previous > position
                    assert b"ANSI SGR" in result
                if choice == 2:
                    for row, sgr, label in [(3, 47, "IMPLICIT"),
                            (5, 101, "RED MARKER"),
                            (7, 102, "GREEN MARKER"),
                            (9, 104, "BLUE MARKER")]:
                        assert f"\x1b[{row};1H\x1b[{sgr};30m{label}".encode() in result
                if choice == 4:
                    horizontal = b'q' * 11
                    vertical = b'x' + (b' ' * 11 + b'x') * 3
                    assert (b'\x1b[3;1H\x1b(0l' + horizontal + b'w' +
                            horizontal + b'w' + horizontal + b'k') in result
                    for row in (4, 5, 6, 7, 8):
                        assert f'\x1b[{row};1H'.encode() + vertical in result, result
                    assert (b'\x1b[6;1Ht' + horizontal + b'n' + horizontal +
                            b'n' + horizontal + b'u') in result
                    assert (b'\x1b[9;1Hm' + horizontal + b'v' + horizontal +
                            b'v' + horizontal + b'j\x1b(B') in result
                    assert b'\x1b)0\x0elqqk x n\x0f ASCII qx\x1b)B' in result
                    assert b'\x1b(0aaaaaa ` fg ~\x1b(B' in result
                    assert b'\x1b(0oo pp qq rr ss\x1b(B' in result
                    check_compact_page(result)
                    os.write(master, b'\r')
                    result = receive(master, b'Enter returns to menu: ')
                    assert b'error' not in result.lower(), result
                    assert b'DEC ALL 32: HEX / ASCII / GLYPH' in result
                    for group in range(4):
                        codes = range(95 + group * 8, 103 + group * 8)
                        row = 2 + group * 3
                        values = b''.join(bytes([c]) + b'  ' for c in codes)
                        assert (f'\x1b[{row};1H'.encode() +
                                ''.join(f'{c:02X} ' for c in codes).encode()) in result
                        assert f'\x1b[{row+1};1H'.encode() + values in result
                        assert (f'\x1b[{row+2};1H\x1b(0'.encode() + values +
                                b'\x1b(B') in result
                    assert b'\x0f\x1b(B\x1b)B\x1b[0m' in result
                if choice in (1, 2, 4):
                    assert b"\x1b[14;1HEnter returns to menu: " in result
                    check_compact_page(result)
                os.write(master, b"\r")
                receive(master, b"Select: ")
            os.write(master, b"Q\r")
            receive(master)
            os.write(master, b"EXIT\r")
            assert process.wait(timeout=10) == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            os.close(master)
    print("Five ASCII programs load; SAVE ,A round-trip; eight menu tests / nine pages pass")


if __name__ == "__main__":
    main()
