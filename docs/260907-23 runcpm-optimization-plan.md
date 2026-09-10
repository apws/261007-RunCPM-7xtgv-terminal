# RunCPM optimization notes and deferred plan

Recorded 2026-09-07. Decision: retain the current implementation; collect useful
application baseline timings before considering optimization. No emulator changes
or additional profiling infrastructure are required now.

## Current architecture and settings

- The active Z80 interpreter is `src/7xtgv-vt100-runcpm/cpu1.h` (Model 1).
  It fetches opcodes from emulated RAM and dispatches through a large switch,
  with additional decoding for prefixed instructions. There is no JIT.
- BIOS, BDOS and the selected internal CCP are native C/C++ compiled for ARM.
  External CP/M commands and applications run through the interpreter.
  `INT_HANDOFF` intercepts RST 08h/10h for BIOS/BDOS calls; these are synchronous
  software handoffs, not asynchronous emulated hardware interrupts.
- RP2350 clock: 150 MHz; compiler optimization: `-O3`; direct single-bank RAM
  access enabled; throttling defaults to zero (maximum interpreter throughput).
  BDOS can change throttling at runtime.
- Emulation is not cycle-accurate. The reported approximately 27 MHz is an
  equivalent Z80 speed for the small startup instruction mix, not a universal
  application speed or an instruction-execution rate.
- The user identifies the MBASIC used for our tests as the 8080 version.
  BBC BASIC and Turbo Pascal 3 use Z80 instructions. Block-opcode frequency in
  these applications has not been measured.

## Findings from the existing optimized ARM binary

Inspected `build/build-vt100-runcpm/7xtgv-vt100-runcpm.ino.elf` using the bundled
ARM objdump/nm tools. Compiler metadata identifies GCC 14.3.0, Cortex-M33 and
`-O3`. These are static assembly findings, not measured optimization gains.
Source line numbers below refer to the version inspected on the date above.

Use `OPTSCAN` as the search tag for any future source comments. No such source
comments have been inserted yet.

| Tag | Location in cpu1.h | Finding and disposition |
| --- | --- | --- |
| OPTSCAN-PCX | 1098 | `PCX = PC` still stores the instruction address to RAM every opcode. BIOS consumes PCX; it is not merely debug state. Investigate updating it only at required handoff/debug boundaries, preserving the original instruction address. First candidate for broad impact. |
| OPTSCAN-THROTTLE | 1063–1085 | The runtime throttle check survives on every opcode. A future unthrottled path could avoid it, but must preserve runtime speed changes. Lower priority than PCX. |
| OPTSCAN-BLOCK | 3462–3555 | LDIR/LDDR and CPIR/CPDR already use native inner loops rather than redispatching each iteration. Refresh updates remain inside them. The inspected LDIR loop has 14 ARM instructions per byte, including four stack accesses and two refresh-update instructions. Batching refresh bookkeeping may also reduce register pressure. Only pursue if application use warrants it. |
| OPTSCAN-REGISTERS | 10–28 and main loop | Emulated registers are globals; the binary contains repeated loads/stores. Keeping hot state local is a possible broader improvement, but synchronization with native services makes it more involved. Deferred. |
| OPTSCAN-DISPATCH | 1125 and prefix handlers | The compiler already emits an ARM jump table. Code layout/size might matter, but there is no demonstrated small dispatch fix. Deferred. |
| OPTSCAN-SWAPS | 1165, 2418, 3075 | XOR swaps already become ordinary loads/stores. Drop this performance candidate. |
| OPTSCAN-MEMORY | 975–991 | Memory helpers are inlined in inspected paths. Active flag lookup tables are already initialized in RAM. No established gain from adding inline keywords or moving these tables into RAM. |

On real Z80 hardware, repeating block instructions can accept interrupts between
iterations. This interpreter's inspected memory block loops do not poll pending
emulated interrupts. Refresh accumulation is therefore a plausible local change,
but must preserve final state; count and search-match checks remain necessary.
Any future block-loop changes must also preserve address wrapping, directional
overlap behavior, zero-count behavior, flags and refresh state. Do not blindly
replace LDIR/LDDR with memcpy or memmove.

Reference: [Zilog Z80 CPU manual](https://www.zilog.com/docs/z80/z80cpu_um.pdf).

## Small application baseline, when wanted

1. Reuse the existing MBASIC arithmetic loop. Historical measurements were
   12.79 seconds with `-Os` and 9.79 seconds with `-O3` for 20,000 iterations.
2. Run an equivalent arithmetic workload in BBC BASIC.
3. Measure Turbo Pascal 3 compilation of a small program and execution of its
   arithmetic workload separately.

For each measurement, repeat three times and record the median, workload,
compiler/firmware settings and correct final result. Avoid printing inside the
timed arithmetic loops. Keep firmware, clock and console conditions fixed.
These are application baselines, not directly comparable CPU benchmarks across
languages. Preserve reusable tests with VT-prefixed names on CP/M and in the
project mirror.

## Conditions for further work

- Stop here if application performance is adequate. Reusing installed test files
  and selecting individual tests is likely more useful for development turnaround
  than speculative CPU tuning.
- If CPU execution becomes a demonstrated bottleneck, try PCX bookkeeping first.
  Consider block-loop refresh accumulation only for workloads that use it enough.
- Change one candidate at a time. Confirm unchanged CPU state and application
  results, then measure with instrumentation disabled. Retain only repeatable
  improvements beyond timing noise and record code-size/RAM tradeoffs.
- Keep Z80 behavior, including refresh-register emulation. No JIT, core rewrite,
  overclocking, or unrelated terminal/disk optimization is part of this plan.
- Commit any later accepted implementation changes with their validation results.
  No performance gain is promised by this document.
