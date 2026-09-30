#!/usr/bin/env python3
"""Exercise the local RunCPM core using disposable CP/M host directories."""

import os
import pty
import select
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def read_until(fd: int, transcript: bytearray, needle: bytes, seconds: float) -> None:
    deadline = time.monotonic() + seconds
    while needle not in transcript:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise AssertionError(f"timed out waiting for {needle!r}; got {transcript[-800:]!r}")
        ready, _, _ = select.select([fd], [], [], remaining)
        if ready:
            transcript.extend(os.read(fd, 8192))


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    benchmark = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="7xtgv-linux-runcpm-") as temporary:
        root = Path(temporary)
        drive = root / "A" / "0"
        drive.mkdir(parents=True)
        shutil.copyfile(benchmark, drive / "VTDRV.COM")
        master, slave = pty.openpty()
        env = dict(os.environ, TERM="xterm")
        process = subprocess.Popen(
            [str(binary)], cwd=root, stdin=slave, stdout=slave, stderr=slave,
            env=env, start_new_session=True,
        )
        os.close(slave)
        transcript = bytearray()
        try:
            read_until(master, transcript, b"A0>", 10)
            transcript.clear()
            os.write(master, b"CCPP\r")
            read_until(master, transcript, b"A0>", 10)
            assert b"CCP prompt: DD" in transcript, transcript[-800:]
            transcript.clear()
            os.write(master, b"CCPP D\r")
            read_until(master, transcript, b"A>", 10)
            transcript.clear()
            os.write(master, b"USER 15\r")
            read_until(master, transcript, b"A>", 10)
            transcript.clear()
            os.write(master, b"CCPP DH\r")
            read_until(master, transcript, b"AF>", 10)
            transcript.clear()
            os.write(master, b"CCPP DD\r")
            read_until(master, transcript, b"A15>", 10)
            transcript.clear()
            os.write(master, b"USER 0\r")
            read_until(master, transcript, b"A0>", 10)
            transcript.clear()
            os.write(master, b"CCPP D EXTRA\r")
            read_until(master, transcript, b"A0>", 10)
            assert b"Usage: CCPP DD|DH|D" in transcript, transcript[-800:]
            transcript.clear()
            os.write(master, b"CCPP D\r")
            read_until(master, transcript, b"A>", 10)
            transcript.clear()
            os.write(master, b"DIR\r")
            read_until(master, transcript, b"A>", 10)
            assert b"VTDRV" in transcript, transcript[-800:]
            transcript.clear()
            os.write(master, b"VTDRV RW\r")
            read_until(master, transcript, b"A>", 30)
            assert b"VTDRV verify: PASS" in transcript, transcript[-1200:]
            assert b"VTDRV write:" in transcript, transcript[-1200:]
            assert b"VTDRV read" in transcript, transcript[-1200:]
            fixture = drive / "VTDRV.$$$"
            before = fixture.read_bytes()
            assert len(before) == 32768
            transcript.clear()
            os.write(master, b"CCPP DD\r")
            read_until(master, transcript, b"A0>", 10)
            transcript.clear()
            os.write(master, b"VTDRV\r")
            read_until(master, transcript, b"A0>", 30)
            assert b"VTDRV verify: PASS" in transcript, transcript[-1200:]
            assert fixture.read_bytes() == before
            os.write(master, b"EXIT\r")
            assert process.wait(timeout=10) == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            os.close(master)
    print("Linux RunCPM: CCPP modes, DIR, VTDRV RW/read-only verify, EXIT passed in disposable filesystem")


if __name__ == "__main__":
    main()
