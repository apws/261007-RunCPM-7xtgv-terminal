# CP/M drive layout

Guest text files are stored in CP/M format: CRLF line endings and a trailing
Ctrl-Z. Maintain that format locally; normal Send/Receive preserves bytes and
does not convert text. New authored files need no 128-byte record padding.
Classify by content, not extension: tokenized BASIC, executable/data files,
disk images and checksummed terminal history must remain byte-exact.
Host-only JSON metadata, this Markdown README and Makefiles retain host format;
historical snapshots remain unchanged. `--cpm-text` is for deliberate external
host-text imports only, not ordinary transfers from this mirror.

These descriptive folders mirror RunCPM drive A user areas: `A0 - CP-M core`
corresponds to the `A0>` prompt (SD directory `A/0`). Type `A1:` to switch
to user 1 and `A0:` to return. The folder suffix uses hexadecimal notation for
the CP/M user number: `AA` is user 10 decimal, `AC` user 12 and `AD` user 13.
The internal CCP searches A0 for commands, so the shared core utilities remain
available from other user areas.

| User | Contents |
|---|---|
| A0 | CP/M core commands, terminal profiles, shared MBASIC/VTESTS, transfer and VT benchmarks |
| A1 | BBC BASIC and its terminal patch source/notes |
| A2 | Turbo Pascal 3.01a and TINST terminal installer |
| A3 | COMAL-80 2.10 demo and its terminal installer (pending board copy) |
| A4 | Assemblers, debugger, Z80 development libraries/manuals |
| A5 | CP/M/CCP sources and related build scripts |
| A6 | CPU benchmarks, Mandelbrot executables, ZEX tests |
| A7 | MBASIC and VT tests |
| A8 | BASIC demos and games, earlier VT demos |
| A9 | Lua launcher/source and examples (runtime support separate) |
| AA | Alternative MBASIC versions (CP/M user 10 decimal) |
| AC | GEEK2350 CODE80 archive; inspect individual CP/M programs before copying |
| AD | GEEK2350 DISK80 binary disk-image archive; never synchronize to the device |

`layout.json` maps original A0 filenames to their intended user area. Files
are copied from the actual board before reorganization, preserving CP/M's
128-byte record padding and tokenized BASIC programs. Human-readable VTESTS
source remains in `../src/7xtgv-vt100-runcpm/tests/VTESTS.BAS`.

Folder and `layout.json` area labels use hexadecimal `0` through `F`.  The
numeric `user` values retained in `progress.json` are RunCPM's decimal user
indices, so its `10` record corresponds to folder `AA`.

`A0:7XTGVTRM.INI` configures the ten named terminal application profiles. The
firmware reads it once during boot; see
`../docs/260910-01 terminal-application-profiles.adoc` for its format.

Use `A7:`, `MBASIC`, then `RUN "VTESTS"` for the maintained visual-test menu:
1 = ink, 2 = paper, 4 = expanded DEC graphics, 8 = Mixworx code.
A7 also holds `VTPAL.BAS` and the ASCII demo sources. Save maintained MBASIC
programs with `SAVE "NAME",A` and load normally; ordinary SAVE is tokenized.

The `VTCPU`, `VTCPU8X`, `VTDRV` and `VTDRV8X` commands are installed in A0 so
the internal CCP can find them from every user area. Duplicates in A6 keep them
with the benchmark collection. CPU tests report active and terminal-paused
passes. Plain `VTDRV` read-tests and verifies a retained 32 KiB `VTDRV.$$$`
fixture; `VTDRV W` recreates it (`RW` remains an alias). `Q` pauses terminal
updates for timed reads, and `W Q` for timed writes and reads; matching firmware
is required. `VTDRV8X` retains the
older write/read/verify/delete test. The `8X` versions use only the common
8080/8085 instruction subset. Sources and protocol notes are in
`../src/RunCPM_7xtgv-terminal/tools/vtbench/`.

BBC BASIC's supplied v5.00 test package needs platform-specific cursor/timer
patching for full terminal integration. Turbo TINST offers an ANSI preset.
COMAL is the demo: SAVE, ENTER and MERGE are disabled; its initial terminal
configuration targets C128 and requires INSTALL for a different terminal.
The Lua files alone do not establish that this firmware enables Lua BDOS calls.
See `../cpm-languages/SOURCES.md` for package provenance.

Earlier board-side checks covered A1, A2 and A4–A6; AA holds the alternate
BASIC binaries. Later ASCII source cleanup moved the maintained demo sources
into A7. These historical transfers are not proof of a current whole-card
mirror: device-side files can change independently. Fetch live INI before
editing it, and fetch BASIC sources when requested before modifying them.
