#!/usr/bin/env python3
"""Patch the checked-in z80asm VTDRV binaries until z80asm is available locally.

The source fix needs one extra `XOR A` before the success RET.  Both .COM files
have record-padded slack after their 128-byte buffer, so the patch relocates the
small remove_file helper there and uses the freed bytes for a jump to `XOR A;RET`.
It refuses any binary whose expected instruction layout differs.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SPECS = (
    (ROOT / 'cpmdrive/A0 - CP-M core/VTDRV.COM', 0x148, 0x149, 0x249),
    (ROOT / 'cpmdrive/A0 - CP-M core/VTDRV8X.COM', 0x15A, 0x15B, 0x25B),
)
SUCCESS = 0x450                 # file offset 0x350 plus CP/M load address
REMOVE = SUCCESS + 2
RELOC_OFFSET = SUCCESS - 0x100
REMOVE_LEN = 11
for path, ret_offset, remove_offset, old_remove in SPECS:
    binary = bytearray(path.read_bytes())
    original = bytes(binary[remove_offset:remove_offset + REMOVE_LEN])
    if len(binary) != 896 or original[:1] != b'\xCD':
        raise SystemExit(f'{path}: unexpected VTDRV binary layout')
    old_call = bytes((0xCD, old_remove & 0xFF, old_remove >> 8))
    matches = [i for i in range(len(binary)) if binary.startswith(old_call, i)]
    if not matches:
        raise SystemExit(f'{path}: no remove_file calls to relocate')
    new_call = bytes((0xCD, REMOVE & 0xFF, REMOVE >> 8))
    for offset in matches:
        binary[offset:offset + 3] = new_call
    binary[ret_offset:ret_offset + 3] = bytes((0xC3, SUCCESS & 0xFF, SUCCESS >> 8))
    binary[RELOC_OFFSET:RELOC_OFFSET + 2] = b'\xAF\xC9'  # XOR A / RET
    binary[RELOC_OFFSET + 2:RELOC_OFFSET + 2 + REMOVE_LEN] = original
    path.write_bytes(binary)
    print(f'patched {path.name}: {len(matches)} remove_file call(s) relocated')
