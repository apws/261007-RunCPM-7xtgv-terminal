#!/usr/bin/env python3
"""Make a personal lowercase-keyword BBC BASIC (Z80) CP/M executable.

This does not alter interpreter code.  BBC BASIC's lexer compares input bytes
directly against its embedded keyword and immediate-command tables, so
changing those tables from ASCII uppercase to lowercase makes lowercase input
tokenise.  It deliberately does not accept uppercase keywords or commands.
The original BBCBASIC.COM is never modified.
"""

from __future__ import annotations

import argparse
from pathlib import Path


TABLE_START = b"\x80AND\x94ABS\x95ACS"
# This byte begins the error-message table immediately after the final
# keyword-table entry (the left-hand pseudo-variable tokens).
TABLE_END = b"\x11Missing "
COMMAND_START = b"\x80AUTO\x81DELETE\x82LIST\x83LOAD"
# COMNDS is followed by the CMDTAB word-address table, whose first word is
# never FFFF in this binary.  The FFFF terminator is therefore unambiguous.
COMMAND_END = b"\xff\xff"


def lowercase_table(result: bytearray, start_marker: bytes, end_marker: bytes,
                    description: str) -> None:
    start = result.find(start_marker)
    if start < 0 or result.find(start_marker, start + 1) >= 0:
        raise ValueError(f"unsupported BBC BASIC binary: {description} signature")
    end = result.find(end_marker, start)
    if end < 0:
        raise ValueError(f"unsupported BBC BASIC binary: {description} end marker")
    for offset in range(start, end):
        byte = result[offset]
        if ord("A") <= byte <= ord("Z"):
            result[offset] = byte + (ord("a") - ord("A"))


def patch_keywords(image: bytes) -> bytes:
    result = bytearray(image)
    lowercase_table(result, TABLE_START, TABLE_END, "keyword-table")
    lowercase_table(result, COMMAND_START, COMMAND_END, "command-table")
    return bytes(result)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="original BBCBASIC.COM")
    parser.add_argument("output", type=Path, help="new BBCLOWER.COM")
    args = parser.parse_args()
    patched = patch_keywords(args.source.read_bytes())
    args.output.write_bytes(patched)
    print(f"wrote {args.output} ({len(patched)} bytes; lowercase keywords and commands)")


if __name__ == "__main__":
    main()
