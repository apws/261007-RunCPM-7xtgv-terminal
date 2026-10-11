# VTCPU and VTDRV CP/M benchmarks

`VTCPU.COM` runs the same fixed Z80 instruction loop twice. The active result
includes normal terminal/TFT/USB contention. The quiet result pauses the
terminal task for the duration of the loop and measures the interpreter core
more directly. The program is callable from CP/M and does not replace or reset
the current emulated machine.

The guest uses private host I/O port `7Bh`. `IN 7Bh` returns `V` as a firmware
capability check; `OUT 7Bh` commands 1, 2 and 3 start an active measurement,
start a quiet measurement and finish/report it. Unknown ports remain ignored.

Plain `VTDRV` is read-only. It reads the existing `VTDRV.$$$` on the current
drive/user, then performs an untimed byte-for-byte verification pass. Run
`VTDRV W` (case-insensitive; `RW` remains a legacy alias) once at a known-safe SD
clock to create or replace the exact 256-record (32 KiB) fixture. The fixture
is deliberately retained for subsequent read-only clock testing.

`VTDRV Q` performs a read-only timed pass with the terminal task paused.
`VTDRV W Q` or `VTDRV Q W` pauses it for *both* timed write and read passes.
The task resumes before untimed verification and result printing, suppressing
cursor and disk-indicator redraws during timing. Options are whole,
case-insensitive tokens; optional slash prefixes and the old `RW` spelling
remain accepted. Unknown, duplicate or glued-together tokens are rejected.
Compare plain `VTDRV` and `VTDRV Q` on one unchanged fixture; quiet mode is
not a raw-card throughput measurement. `W` intentionally recreates the file.

The COM probes private port `7Eh` before any quiet pass. Old quiet-read-only
firmware returns `Q`; firmware that supports quiet read *and* write returns
`B`. A quiet write requires `B`, so a new COM cannot silently write without
quiet timing on old firmware. Either `Q` or `B` permits quiet read.

Timed passes include CP/M BDOS create/open/close calls and the complete RunCPM
host filesystem and physical-drive path. Verification is untimed so interpreted
byte comparisons do not distort the read rate. Read or verification failures do
not delete or otherwise modify the fixture.

`VTCPU8X` and `VTDRV8X` restrict their guest code to instructions shared by the
8080 and 8085. Opcode compatibility does not imply identical cycle timing: for
example, register `MOV` takes 5 states on an 8080 and 4 on an 8085, while `DCX`
takes 5 and 6 respectively. Conditional-jump timing also differs. Therefore
`VTCPU8X` reports loop throughput rather than converting the result to one
nominal 8080/8085 clock rate. See Intel's *8080/8085 Assembly Language
[Programming Guide](https://st.sdf-eu.org/i8080/Intel%208080-8085%20Assembly%20Language%20Programming%201977%20Intel.pdf)
for the per-instruction tables.

The drive benchmark uses private port `7Ah`. `IN 7Ah` returns `D`; output
commands 1/2 start/end writing, 3/4 start/end reading, and 255 cancels after an
error. The `8X` variant uses commands 5/6 and 7/8 for the same operations so
the host can label its results; quiet read uses commands 9/10 and quiet write
uses 11/12. Cancel 255 resumes a paused terminal after an I/O error. The host
reports elapsed microseconds and KiB/s. Normal JC8048W535 builds now use
SPI3/SdFat with the requested clock selected by `sdfat_spi` (20 MHz fallback).
The former one-bit SD_MMC build alternative has been removed. Results always
describe the firmware actually running, including its storage path, rather
than the card's advertised maximum speed.

The file workload and verification are portable CP/M 2.2 BDOS operations, but
automatic microsecond timing is not: standard CP/M 2.2 has no portable clock
API. Another emulator or native 8080/8085 system needs a platform timer adapter,
terminal timestamps, or an external stopwatch around the same workload.

These sources use Z80 mnemonics and therefore use the `.Z80` suffix. On CP/M,
SLR `Z80ASM NAME` defaults to `NAME.Z80` and directly creates `NAME.COM`.
Digital Research `ASM NAME` instead defaults to `NAME.ASM`, accepts Intel 8080
mnemonics, and emits `NAME.HEX`/`NAME.PRN`; it must not be used for these files.

Canonical sources now live in repository `cpmcard/C/1` (`7xtgv(C1)`), not
beside this README. Shared installed commands live in `cpmcard/A/0`; the disk
fixture and imported exercisers live in `bench(C3)`. Old host source/binary
duplicates were retired. Build in a disposable copy of C1, not the live card.

On CP/M, make `cpmdev(A1)`'s SLR assembler available in that disposable work
area and run `Z80ASM VTDRV`. The focused host test also assembles and verifies
it: `python3 tests/test_vtdrv_q.py` from the firmware directory. Add `--install`
only to replace the local card A0 binary deliberately.

Alternatively, in a temporary host copy of the C1 sources, use Albert Veli's
`z80asm` (convert guest CRLF/Ctrl-Z text to host text in that copy if needed):

```sh
z80asm -o VTCPU.COM VTCPU.Z80
z80asm -o VTDRV.COM VTDRV.Z80
z80asm -o VTCPU8X.COM VTCPU8X.Z80
z80asm -o VTDRV8X.COM VTDRV8X.Z80
z80asm -o VTTERM.COM VTTERM.Z80
z80asm -o REBOOT.COM REBOOT.Z80
```
