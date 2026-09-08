# VTMHZ CP/M benchmark

`VTMHZ.COM` runs the same fixed Z80 instruction loop twice. The active result
includes normal terminal/TFT/USB contention. The quiet result pauses the
terminal task for the duration of the loop and measures the interpreter core
more directly. The program is callable from CP/M and does not replace or reset
the current emulated machine.

The guest uses private host I/O port `7Bh`. `IN 7Bh` returns `V` as a firmware
capability check; `OUT 7Bh` commands 1, 2 and 3 start an active measurement,
start a quiet measurement and finish/report it. Unknown ports remain ignored.

Build with Albert Veli's `z80asm`:

```sh
z80asm -o VTMHZ.COM VTMHZ.ASM
```
