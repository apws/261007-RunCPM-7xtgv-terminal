#!/usr/bin/env python3
"""Assemble/test VTPAL in disposable Linux RunCPM storage, never on device.

This tests guest byte output only, not the embedded 7xtgv renderer.
Requires the existing build/RunCPM_linux/RunCPM comparison executable.
"""
import os
from pathlib import Path
import pty
import select
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
CORE = ROOT / "cpmdrive/A0 - CP-M core"


def receive(fd, needle=b"A0>", timeout=10):
    data = bytearray()
    until = time.monotonic() + timeout
    while needle not in data:
        remaining = until - time.monotonic()
        if remaining <= 0:
            raise RuntimeError(f"Guest timeout: {data[-1000:]!r}")
        if select.select([fd], [], [], remaining)[0]:
            data.extend(os.read(fd, 8192))
    return bytes(data)


def main():
    with tempfile.TemporaryDirectory(prefix="xtgv-vtpal-") as temporary:
        card = Path(temporary)
        drive = card / "A/0"
        drive.mkdir(parents=True)
        shutil.copyfile(ROOT / "cpmdrive/A4 - Assembler and development tools/ASM.COM", drive / "ASM.COM")
        shutil.copyfile(CORE / "LOAD.COM", drive / "LOAD.COM")
        source = (CORE / "VTPAL.ASM").read_text()
        (drive / "VTPAL.ASM").write_bytes(source.replace("\n", "\r\n").encode("ascii") + b"\x1a")
        master, slave = pty.openpty()
        process = subprocess.Popen([str(ROOT / "build/RunCPM_linux/RunCPM")],
            cwd=card, stdin=slave, stdout=slave, stderr=slave,
            env=dict(os.environ, TERM="xterm"), start_new_session=True)
        os.close(slave)
        try:
            receive(master)
            for command in ("ASM VTPAL", "LOAD VTPAL"):
                os.write(master, command.encode() + b"\r")
                print(receive(master).decode("ascii", errors="replace"))
            assert (drive / "VTPAL.COM").is_file()
            for argument in [""] + [str(i) for i in range(10)] + ["  3  ", "10", "-1", "A", "1 2"]:
                os.write(master, ("VTPAL " + argument + "\r").encode())
                data = receive(master)
                if argument in ("10", "-1", "A", "1 2"):
                    assert b"VTPAL [0..9]" in data and b"\x1b[>69;" not in data, data
                else:
                    digit = argument.strip() or "0"
                    assert ("\x1b[>69;" + digit + "v").encode() in data, data
            os.write(master, b"EXIT\r")
            assert process.wait(timeout=10) == 0
            shutil.copyfile(drive / "VTPAL.COM", CORE / "VTPAL.COM")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            os.close(master)
    print("VTPAL assembled; 16 argument/output checks passed; local COM updated")


if __name__ == "__main__":
    main()
