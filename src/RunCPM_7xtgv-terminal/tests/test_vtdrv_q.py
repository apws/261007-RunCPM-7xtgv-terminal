#!/usr/bin/env python3
"""Assemble VTDRV with CP/M Z80ASM and check its old-firmware fallback."""

import os
import argparse
import pty
import select
import shutil
import subprocess
import tempfile
import time
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "build/RunCPM_linux/RunCPM"
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from card_paths import card_root, require_files
ASSEMBLER = card_root() / "A/4/Z80ASM.COM"
SOURCE = ROOT / "src/RunCPM_7xtgv-terminal/tools/vtbench/VTDRV.Z80"


def read_until(fd, transcript, needle, timeout=15):
    deadline = time.monotonic() + timeout
    while needle not in transcript:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise AssertionError(f"waiting for {needle!r}: {transcript[-1200:]!r}")
        ready, _, _ = select.select([fd], [], [], remaining)
        if ready:
            transcript.extend(os.read(fd, 8192))


def command(fd, transcript, line, needle=b"A0>", timeout=15):
    transcript.clear()
    os.write(fd, line.encode("ascii") + b"\r")
    read_until(fd, transcript, needle, timeout)
    return bytes(transcript)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true", help="copy tested COM to local cpmcard/A/0")
    args = parser.parse_args()
    require_files(ASSEMBLER, SOURCE)
    with tempfile.TemporaryDirectory(prefix="7xtgv-vtdrv-q-") as temp:
        drive = Path(temp) / "A" / "0"
        drive.mkdir(parents=True)
        shutil.copyfile(ASSEMBLER, drive / "Z80ASM.COM")
        shutil.copyfile(SOURCE, drive / "VTDRV.Z80")
        master, slave = pty.openpty()
        process = subprocess.Popen(
            [str(RUNTIME)], cwd=temp, stdin=slave, stdout=slave, stderr=slave,
            env=dict(os.environ, TERM="xterm"), start_new_session=True,
        )
        os.close(slave)
        transcript = bytearray()
        try:
            read_until(master, transcript, b"A0>")
            result = command(master, transcript, "Z80ASM VTDRV", timeout=30)
            assert b"0 Error(s) Detected." in result, result[-1200:]
            binary = drive / "VTDRV.COM"
            assert binary.is_file() and binary.stat().st_size > 0, result[-1200:]
            result = command(master, transcript, "VTDRV W", timeout=30)
            assert b"VTDRV verify: PASS" in result, result[-1200:]
            assert b"VTDRV write:" in result, result[-1200:]
            fixture = drive / "VTDRV.$$$"
            before = fixture.read_bytes()
            assert len(before) == 32768
            result = command(master, transcript, "VTDRV RW", timeout=30)
            assert b"VTDRV verify: PASS" in result, result[-1200:]
            assert b"VTDRV write:" in result, result[-1200:]
            result = command(master, transcript, "VTDRV")
            assert b"VTDRV verify: PASS" in result, result[-1200:]
            for option in ("Q", "W Q", "Q W", "/W /Q", "rw q"):
                result = command(master, transcript, f"VTDRV {option}")
                assert b"VTDRV Q needs newer" in result, result[-1200:]
            for option in ("QW", "W NOPE", "W W", "Q Q", "RWQ"):
                result = command(master, transcript, f"VTDRV {option}")
                assert b"Usage: VTDRV [W] [Q]" in result, result[-1200:]
            assert fixture.read_bytes() == before
            if args.install:
                shutil.copyfile(binary, card_root() / "A/0/VTDRV.COM")
            command(master, transcript, "EXIT", needle=b"\r\n", timeout=10)
            assert process.wait(timeout=10) == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            os.close(master)
    print("VTDRV: assembled; W/RW/default passed; quiet/malformed options safely rejected; fixture unchanged")


if __name__ == "__main__":
    main()
