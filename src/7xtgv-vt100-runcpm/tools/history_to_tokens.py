#!/usr/bin/env python3
"""Export one bank of 7XTGVTRM.BUF as a CP/M-readable token list."""

import argparse
import re
import zlib
from pathlib import Path


def entries(data: bytes, bank: int) -> list[bytes]:
    if not data.startswith(b"# 7xtgv terminal history v1\n") and not data.startswith(
        b"# 7xtgv terminal history v2\n"
    ):
        raise ValueError("not a 7xtgv terminal history file")
    if data.startswith(b"# 7xtgv terminal history v2\n"):
        footer = re.search(rb"(?m)^# crc32=([0-9A-Fa-f]{8})\n", data)
        if footer is None:
            raise ValueError("history v2 has no CRC footer")
        expected = int(footer.group(1), 16)
        if zlib.crc32(data[: footer.start()]) != expected:
            raise ValueError("history CRC mismatch")
        # CP/M file transfer may pad the final 128-byte record with NULs.
        data = data[: footer.start()]
    selected = False
    result: list[bytes] = []
    for line in data.replace(b"\r\n", b"\n").split(b"\n"):
        if re.fullmatch(rb"\[[0-9]\]", line):
            selected = line == f"[{bank}]".encode("ascii")
        elif selected and line.startswith(b">") and len(line) > 1:
            result.append(line[1:])
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("history", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--bank", type=int, choices=range(10), default=5)
    args = parser.parse_args()
    tokens = entries(args.history.read_bytes(), args.bank)
    if not tokens:
        raise SystemExit(f"bank {args.bank} has no entries; output not changed")
    args.output.write_bytes(b"\r\n".join(tokens) + b"\r\n\x1a")
    print(f"{len(tokens)} entries from bank {args.bank} -> {args.output}")


if __name__ == "__main__":
    main()
