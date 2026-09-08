# CP/M drive layout

These descriptive folders mirror RunCPM drive A user areas: `A0 - CP-M core`
corresponds to the `A0>` prompt (SD directory `A/0`). Type `A1:` to switch
to user 1 and `A0:` to return. The internal CCP searches A0 for commands,
so the shared core utilities remain available from other user areas.

| User | Contents |
|---|---|
| A0 | CP/M core commands, shared MBASIC/VTESTS, console transfer and VTMHZ helpers |
| A1 | BBC BASIC and its terminal patch source/notes |
| A2 | Turbo Pascal 3.01a and TINST terminal installer |
| A3 | COMAL-80 2.10 demo and its terminal installer (pending board copy) |
| A4 | Assemblers, debugger, Z80 development libraries/manuals |
| A5 | CP/M/CCP sources and related build scripts |
| A6 | CPU benchmarks, Mandelbrot executables, ZEX tests |
| A7 | MBASIC and VT tests |
| A8 | BASIC demos and games, earlier VT demos |
| A9 | Lua launcher/source and examples (runtime support separate) |
| A10 | Alternative MBASIC versions |

`layout.json` maps original A0 filenames to their intended user area. Files
are copied from the actual board before reorganization, preserving CP/M's
128-byte record padding and tokenized BASIC programs. Human-readable VTESTS
source remains in `../src/7xtgv-vt100-runcpm/tests/VTESTS.BAS`.

Use `A0:`, `MBASIC`, `RUN "VTESTS"` for the shared menu. A7 contains a
clean duplicate workspace with only `MBASIC.COM` and `VTESTS.BAS`; use `A7:`
then `MBASIC` and `RUN "VTESTS"` when testing that pair in isolation. 1 = ink,
8 = Mixworx code.

`VTMHZ.COM` is installed in A0 so the internal CCP can find it from every user
area. A duplicate in A6 keeps it with the CPU benchmark collection. It reports
one measurement with the terminal active and one with TFT/USB rendering paused;
its source and protocol notes are in
`../src/7xtgv-vt100-runcpm/tools/vtmhz/`.

BBC BASIC's supplied v5.00 test package needs platform-specific cursor/timer
patching for full terminal integration. Turbo TINST offers an ANSI preset.
COMAL is the demo: SAVE, ENTER and MERGE are disabled; its initial terminal
configuration targets C128 and requires INSTALL for a different terminal.
The Lua files alone do not establish that this firmware enables Lua BDOS calls.
See `../cpm-languages/SOURCES.md` for package provenance.

Board-side copies were verified for A1, A2, and A4–A6. The physical board and
project mirror now agree on the exchanged areas: A7 contains only MBASIC and
VTESTS, while A10 contains the seven alternate BASIC binaries. A8 received its
demo files, but CP/M returned `Bdos Err on A: R/O` while erasing the original
A0 copies, so cleanup stopped and those duplicates remain safely in place.
A3's board copy is pending.
