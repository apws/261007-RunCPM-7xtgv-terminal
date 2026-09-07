# Validation — 2026-09-07

## Firmware

- Built with the pinned `rp2350-touch-lcd-2` profile after final source changes.
- Program storage: **158,184 bytes**.
- Static RAM: **129,756 bytes** (24%); linker reports 394,532 bytes remaining.
  This is the build report, not a measurement of peak runtime stack/heap usage.
- UF2: `../../build/build-vt100-runcpm/7xtgv-vt100-runcpm.ino.uf2`.
- SHA-256: `aa932f6086c78e8c51338b241bf1e3fba579aad730bec92dfb914845b199ea46`.
- Flashed successfully through the identified RP2350 BOOTSEL volume.
- Prior demo/backlight firmware remains available for rollback.

## Automated host checks — passed

- Parser, keyboard decoder, screen bounds and attributes, manual panning,
  fragmented and malformed escape handling, ANSI/VT52 replies and modes.
- 200,000 deterministic hostile input bytes under AddressSanitizer/UBSan.
- One million ordered bytes through the production SPSC queue implementation
  while the opposite direction is full; no deadlock or byte loss.
- Production runtime with fake USB/display peripherals: bounded rendering,
  partial/slow mirror writes, disconnected USB, reconnect snapshots, saturated
  CP/M input, deliberately overflowing terminal replies, and worst-case mirror
  row sizing. AddressSanitizer/UBSan passed.
- Five Python helper tests, including actual pseudo-TTY keyboard/mirror I/O,
  mode/pan forwarding, clean exit, and restoration of terminal settings.
  Only USB modem ioctls are substituted in the pseudo-TTY test.
- Source whitespace checks passed.

## Physical RP2350 checks over USB — passed

The attached board booted to CP/M before a PC connected. The screen mirror
reported build `7xtgv-vt100-v1 / Sep 7 2026-00:13:26`, SD initialization at
19 MHz, and an estimated Z80 speed of 20 MHz in its startup benchmark.

`tests/hardware_smoke.py` exercised the actual two-core firmware:

1. Attach to the already booted `A0>` session.
2. `DIR` successfully lists SD files.
3. `MBASIC` launches BASIC-80 5.30; `PRINT 2+2` returns 4.
4. ANSI erase/position/text produces `ANSI-OK` on the cleared screen.
5. `RIGHT-EDGE` appears at logical row 6, columns 71–80.
6. Send pan shortcuts, then evaluate `PRINT 3+4`: result 7 confirms the
   reserved pan sequences did not leak into CP/M input.
7. Enter VT52, clear and address row 9/column 11, display `VT52-OK`, and return
   to ANSI mode. The full PC mirror preserves the requested coordinates.
8. `SYSTEM` returns to CP/M.
9. Disconnect/reconnect USB; the existing session remains at `A0>`.

Snapshots and assertions are preserved in
[the hardware smoke log](tests/hardware-smoke-2026-09-07.json).
Tests used transient BASIC commands and did not write SD files. The serial
connection was closed afterward so the PC helper can open it.

## Physical visual acceptance

USB tests verify firmware screen state, not the optical appearance of the TFT.
Physical text readability, colors, cursor, pan position and idle backlight await
user confirmation. Host-rendered previews were inspected for text, underline,
reverse and line-drawing behavior. No full-screen legacy CP/M application beyond
MBASIC has yet been certified against this subset.

## VTESTS menu — 2026-09-07

Uploaded and saved VTESTS.BAS through BASIC-80 5.30, cleared program memory,
and reloaded the saved SD copy. Verified all seven individual choices and
Enter returning to the menu, invalid input returning to the menu, lowercase
a running all seven stages through ALL VT TESTS COMPLETE, and lowercase q
returning to CP/M. Relaunched and left the ink page waiting for Enter.
Checks observed the USB screen mirror with explicit refreshes; physical color
appearance remains a visual check on the TFT. No firmware palette change was
made during this menu update. The separate BASIC source fixtures were replaced
by this single self-contained suite.

## Bright normal / former normal as dark palette — 2026-09-07

Restored original bright normal RGB entries; red, green, blue, silver, cyan,
magenta and yellow dark/bold entries now use the preceding normal RGB values.
Dedicated black remains black. Renamed ink fixture labels to NORMAL SILVER /
DARK SILVER and removed a duplicate print. Firmware profile build passed;
terminal, keyboard, bounds/fuzz and bidirectional queue tests passed outside
the ptrace sandbox (required by LeakSanitizer). Saved/reloaded the updated
VTESTS.BAS, flashed the UF2 through BOOTSEL, and launched only menu option 1.

## Photo-guided ink refinement — 2026-09-07

Normal gray is now (95,95,95), bold gray remains pure black; normal silver
(230,230,230), normal yellow (230,230,50), dark yellow (190,185,0).
Ink sample PRINT statements now end their logical lines so MBASIC's output
column accounting does not wrap continuous escape-bearing prints. Profile
build passed; updated VTESTS saved and firmware flashed. Ran only ink test;
USB mirror assertions confirmed DARK YELLOW at row 9, column 25 and NORMAL
SILVER at row 17, column 1. Left the test displayed for physical inspection.

## Mixworx palette and CODE page — 2026-09-07

Normal gray raised from 95 to 100. Normal green/cyan channels reduced about
5%; all non-black dark entries raised about 5%. Added menu option 8 and
extended Run All to eight tests. The code fixture uses explicit per-token
cursor positioning and SGR 1 + gray for black keywords. Profile build passed.
Verified every one of the 18 rendered code lines against mixworx-example.txt
in the serial screen mirror; every line fits in 52 columns. A menu line-number
collision discovered during verification was corrected in source and on disk.
The source now also passes unique, increasing line-number and <=80 source-line
length checks. Physical color balance is still a user visual check.

## Speed build and CP/M organization — 2026-09-07

Changed the RP2350 profile from `-Os` to `-O3` at the existing 150 MHz system
clock. The identical 20,000-iteration MBASIC arithmetic loop improved from
12.79 s to 9.79 s (about 31%). Direct single-bank RAM access was already
enabled and CPU throttling was already disabled. Fixed `_sys_extendfile`: its
old unsigned size subtraction could wrap when extending a file; the new code
zero-fills only the positive difference in bounded 128-byte writes. A focused
fake-file regression test and the complete Python helper suite pass.

The project mirror is `../../cpmdrive/` with descriptive `A0 - ...` through
`A9 - ...` folders. Board-side copies were verified for A1, A2, and A4–A7;
A8 copies succeeded but the subsequent A0 erase returned CP/M `Bdos Err on A:
R/O`, so cleanup stopped and originals were preserved.
