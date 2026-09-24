#!/usr/bin/env python3
"""Run VTPAL.BAS in isolated CP/M MBASIC; verify guest bytes, not hardware."""
import os
from pathlib import Path
import pty
import re
import shutil
import subprocess
import tempfile

from build_vtpal import ROOT, receive

SOURCE = ROOT / "cpmdrive/A7 - MBASIC and VT tests"
END_PAGE = b"Choose preferred preset last."


def main():
    data = (SOURCE / "VTPAL.BAS").read_bytes()
    body, eof, tail = data.partition(b"\x1a")
    # Guest SAVE ,A includes a CP/M record tail, possibly stale bytes.
    # Only bytes preceding the first Ctrl-Z are source text.
    assert eof
    assert b"\n" not in body.replace(b"\r\n", b"")
    source = body.decode("ascii")
    assert all(len(line) <= 80 for line in source.splitlines())
    with tempfile.TemporaryDirectory(prefix="xtgv-vtpal-basic-") as temporary:
        drive = Path(temporary) / "A/0"
        drive.mkdir(parents=True)
        shutil.copyfile(SOURCE / "MBASIC.COM", drive / "MBASIC.COM")
        shutil.copyfile(SOURCE / "VTPAL.BAS", drive / "VTPAL.BAS")
        master, slave = pty.openpty()
        process = subprocess.Popen([str(ROOT / "build/linux_RunCPM/RunCPM")],
            cwd=temporary, stdin=slave, stdout=slave, stderr=slave,
            env=dict(os.environ, TERM="xterm"), start_new_session=True)
        os.close(slave)
        try:
            receive(master)
            os.write(master, b"MBASIC VTPAL\r")
            data = receive(master, END_PAGE)
            assert b"\x1b[>69;0v" in data
            normal = [30,31,32,34,37,36,35,33]
            for fg in normal + [n+60 for n in normal]:
                for bg in (47,41,42,44):
                    assert f"\x1b[0;{fg};{bg}m".encode() in data, repr(data[:1600])
            assert data.count(b" Aa09{}[]+-= ") == 64
            assert b"NORMAL INK" in data and b"ALT INK" in data
            screen = data[data.index(b"\x1b<"):]
            assert b"\n" not in screen and b"\r" not in screen
            for digit in range(10):
                os.write(master,str(digit).encode())
                data = receive(master, END_PAGE)
                assert f"\x1b[>69;{digit}v".encode() in data
            os.write(master,b"i")
            data=receive(master,END_PAGE)
            assert b"NORMAL / ALT INK PAIRS" in data
            assert data.count(b" Aa09 {}[] +-= ijMW ") == 16
            for bg in (41,42,44,47):
                os.write(master,b"b")
                data=receive(master,END_PAGE)
                for fg in normal+[n+60 for n in normal]:
                    assert f"\x1b[0;{fg};{bg}m".encode() in data
                for row,col in re.findall(rb"\x1b\[(\d+);(\d+)H",data):
                    assert 1<=int(row)<=23 and 1<=int(col)<=80
            os.write(master,b"p")
            assert receive(master,END_PAGE).count(b" Aa09{}[]+-= ")==64
            os.write(master,b"r")
            assert receive(master,END_PAGE).count(b" Aa09{}[]+-= ")==64
            os.write(master,b"q")
            data=receive(master,b"OK\r")
            assert b"\x1b[?25h" in data and b"Back in MBASIC" in data
            os.write(master,b"SYSTEM\r")
            receive(master)
            os.write(master,b"EXIT\r")
            assert process.wait(timeout=10)==0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            os.close(master)
    print("VTPAL.BAS: CP/M MBASIC matrix, pairs, papers, 10 presets, redraw and exit passed")


if __name__ == "__main__":
    main()
