# VTCPU and VTDRV CP/M benchmarks

`VTCPU.COM` runs the same fixed Z80 instruction loop twice. The active result
includes normal terminal/TFT/USB contention. The quiet result pauses the
terminal task for the duration of the loop and measures the interpreter core
more directly. The program is callable from CP/M and does not replace or reset
the current emulated machine.

The guest uses private host I/O port `7Bh`. `IN 7Bh` returns `V` as a firmware
capability check; `OUT 7Bh` commands 1, 2 and 3 start an active measurement,
start a quiet measurement and finish/report it. Unknown ports remain ignored.

`VTDRV.COM` creates `VTDRV.$$$` on the current drive/user, sequentially writes
and reads 256 CP/M records (32 KiB), verifies a second read pass, and deletes the
temporary file. Its timed passes include BDOS create/open/close calls and the
complete RunCPM host filesystem and physical-drive path. Verification is
untimed so interpreted byte comparisons do not distort the read rate.

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
the host can label its results. The host reports elapsed microseconds and
KiB/s. The current
JC8048W535 profile uses one-bit SDMMC at 20 MHz, so results describe that wiring
and configuration rather than the card's advertised maximum speed.

The file workload and verification are portable CP/M 2.2 BDOS operations, but
automatic microsecond timing is not: standard CP/M 2.2 has no portable clock
API. Another emulator or native 8080/8085 system needs a platform timer adapter,
terminal timestamps, or an external stopwatch around the same workload.

Build with Albert Veli's `z80asm`:

```sh
z80asm -o VTCPU.COM VTCPU.ASM
z80asm -o VTDRV.COM VTDRV.ASM
z80asm -o VTCPU8X.COM VTCPU8X.ASM
z80asm -o VTDRV8X.COM VTDRV8X.ASM
```
