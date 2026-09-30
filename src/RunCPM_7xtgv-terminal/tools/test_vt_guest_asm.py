#!/usr/bin/env python3
"""Assemble the project-owned display helpers in disposable native RunCPM."""
import os
import pty
import select
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RUNCPM = ROOT / "build/RunCPM_linux/RunCPM"
DRIVE = ROOT / "cpmdrive/A0 - CP-M core"
ASM = ROOT / "cpmdrive/A4 - Assembler and development tools/ASM.COM"


def until_bytes(fd, needle, timeout=15):
    output = bytearray()
    deadline = time.monotonic() + timeout
    while needle not in output:
        left = deadline - time.monotonic()
        if left <= 0:
            raise AssertionError(f"RunCPM timed out: {output[-700:]!r}")
        if select.select([fd], [], [], left)[0]:
            output.extend(os.read(fd, 8192))
    return bytes(output)


def until_prompt(fd, timeout=15):
    return until_bytes(fd, b"A0>", timeout)


def main():
    if not RUNCPM.is_file():
        raise SystemExit("Build native RunCPM first: make -C src/RunCPM_linux")
    with tempfile.TemporaryDirectory(prefix="7xtgv-vt-asm-") as temp:
        root = Path(temp)
        drive = root / "A/0"
        drive.mkdir(parents=True)
        for source in (ASM, DRIVE / "LOAD.COM", DRIVE / "VTINFO.ASM",
                       DRIVE / "VTZOOM.ASM"):
            shutil.copyfile(source, drive / source.name)
        master, slave = pty.openpty()
        process = subprocess.Popen(
            [str(RUNCPM)], cwd=root, stdin=slave, stdout=slave, stderr=slave,
            env=dict(os.environ, TERM="xterm"), start_new_session=True)
        os.close(slave)
        try:
            until_prompt(master)
            for name in ("VTINFO", "VTZOOM"):
                os.write(master, f"ASM {name}\r".encode())
                assembled = until_prompt(master)
                if b"ERROR" in assembled.upper() or b"?" in assembled:
                    raise AssertionError(f"{name} assembly: {assembled[-1000:]!r}")
                os.write(master, f"LOAD {name}\r".encode())
                linked = until_prompt(master)
                if b"ERROR" in linked.upper():
                    raise AssertionError(f"{name} load: {linked[-1000:]!r}")
                if not (drive / f"{name}.COM").is_file():
                    raise AssertionError(f"{name}.COM was not produced")
            record = (b"\x1bP7xtgv-info;1;RP2_147;rp2350-lcd-1-47-a;"
                      b"260925-2025;320;172;2;80;24;26;8;0;0;0;"
                      b"80;24;80;24;26;5;20;4\x1b\\")
            os.write(master, b"VTINFO\r")
            until_bytes(master, b"\x1b[>72v")
            os.write(master, record)
            report = until_prompt(master)
            for expected in (b"Display : RP2_147", b"Panel   : 320x172",
                             b"View    : zoom 2, logical 80x24, visible 26x8",
                             b"1x: 80x24", b"4x: 20x4"):
                if expected not in report:
                    raise AssertionError(f"VTINFO lacks {expected!r}: {report[-1200:]!r}")
            os.write(master, b"VTZOOM 2 80x24\r")
            sent = until_bytes(master, b"\x1b[>72v")
            if b"\x1b[>73;2;80;24v" not in sent:
                raise AssertionError(f"VTZOOM command incorrect: {sent[-700:]!r}")
            os.write(master, record)
            report = until_prompt(master)
            if b"View    : zoom 2, logical 80x24, visible 26x8" not in report:
                raise AssertionError(f"VTZOOM query incorrect: {report[-700:]!r}")
            for command, expected in ((b"VTZOOM 3\r", b"\x1b[>73;3v"),
                                      (b"VTZOOM DEFAULT\r", b"\x1b[>73;0v"),
                                      (b"VTZOOM default\r", b"\x1b[>73;0v"),
                                      (b"VTZOOM\r", b"")):
                os.write(master, command)
                sent = until_bytes(master, b"\x1b[>72v")
                if expected and expected not in sent:
                    raise AssertionError(f"{command!r} sent {sent[-700:]!r}")
                os.write(master, record)
                if b"View    : zoom 2, logical 80x24, visible 26x8" not in until_prompt(master):
                    raise AssertionError(f"{command!r} did not report the reply")
            os.write(master, b"VTINFO\r")
            until_bytes(master, b"\x1b[>72v")
            os.write(master, b"\x1bP7xtgv-info;1;RP2_147\x1b\\")
            report = until_prompt(master)
            if b"VTINFO: BAD REPLY" not in report:
                raise AssertionError(f"Malformed reply accepted: {report[-700:]!r}")
            os.write(master, b"VTINFO\r")
            until_bytes(master, b"\x1b[>72v")
            if b"VTINFO: NOT SUPPORTED" not in until_prompt(master, timeout=15):
                raise AssertionError("Unanswered query did not time out")
            for name in ("VTINFO", "VTZOOM"):
                shutil.copyfile(drive / f"{name}.COM", DRIVE / f"{name}.COM")
            os.write(master, b"EXIT\r")
            if process.wait(timeout=10):
                raise AssertionError("RunCPM exited unsuccessfully")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            os.close(master)
    print("VTINFO/VTZOOM assembled; valid, malformed and timeout replies exercised")


if __name__ == "__main__":
    main()
