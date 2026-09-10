# Interpreter porting and internal-SRAM policy

This note records the memory-placement model for RunCPM and for a future VMEX
port to ESP32-S3 and other Espressif MCUs. RunCPM is a useful prototype because
its Z80 interpreter, native operating-system services, guest memory and
terminal form the same broad classes of workload expected in VMEX.

## Separate the execution paths

Treat the system as four paths with different costs:

1. The guest interpreter fetch/dispatch/execute loop runs continuously while a
   guest program computes. Its instruction code and hottest VM state benefit
   directly from deterministic internal SRAM access.
2. Native runtime services run only when the guest requests them. In RunCPM
   these are the native BIOS and BDOS; in VMEX these will include primitives,
   object/value operations and host services. Small, frequent services can be
   as important as the interpreter dispatch loop.
3. Storage and peripheral services enter filesystem, SD, USB or display-driver
   code. Their wall time is often dominated by the device or driver rather than
   instruction fetch.
4. Framebuffers, guest heaps and other large capacity data belong in external
   PSRAM when they do not fit internally. Access patterns should be sequential
   or tiled so the external-memory cache can burst effectively.

Internal instruction SRAM and internal data SRAM are scarce, explicitly
managed fast memory. There is no automatic eviction to flash or PSRAM. Moving
code into internal instruction SRAM also moves the linker boundary and reduces
the internal data/heap budget by a comparable amount, so placement must follow
measurement rather than subsystem names alone.

## Current RunCPM sizes on ESP32-S3

These figures come from the current linked IRAM build
`build/build-vt100-runcpm-s3-dual/7xtgv-vt100-runcpm.ino.elf` made on
2026-09-09. They are linked Xtensa code sizes, not source sizes.

| Native function | Text body | Literal pool | Effective placement size |
| --- | ---: | ---: | ---: |
| `Z80run()` | 28,802 B | about 200 B | about 29,002 B |
| `_Bios()` | 547 B | 36 B | 583 B |
| `_Bdos()` | 2,186 B | 40 B | 2,226 B |
| BIOS + BDOS dispatchers | 2,733 B | 76 B | 2,809 B |

The BIOS and BDOS dispatchers together are only about 9.7% of the internal-SRAM
cost already accepted for `Z80run()`. They currently remain in cached flash.

The current Arduino-ESP32 build uses its default `-Os`; the RP2350 profile is
explicitly `-O3`. Compile-only S3 comparison builds produced:

| Sketch optimization | Program bytes | Total IRAM text | `Z80run()` body | Linker internal-heap window |
| --- | ---: | ---: | ---: | ---: |
| `-Os` (current firmware) | 566,000 | 101,867 B | 28,802 B | 113,784 B |
| `-O2` candidate | 596,316 | 104,931 B | 31,868 B | 110,664 B |
| `-O3` hardware-tested | 625,852 | 105,139 B | 32,076 B | 110,408 B |

The heap-window figures are the linked space from `_heap_start` to the end of
the static internal-memory region, before runtime allocations. They are not a
runtime free-heap measurement. Because executable IRAM and data RAM alias the
same physical SRAM banks, the larger optimized interpreter reduces this window
by about 3.1--3.4 KiB. Both candidates fit comfortably. Their performance must
be measured on hardware; the larger global `-O3` code can also increase cached
flash pressure in native services and terminal code.

The standardized A0 hardware series measured global `-O3` at 19.30/19.34 MHz
and 7.32/12.45 KiB/s storage, global `-O2` at 19.38 MHz and 7.46/12.61 KiB/s,
and targeted `Z80run()`-only O2 at 19.01/19.05 MHz and 7.53/12.64 KiB/s. The
original A0 `-Os` control was 17.30/17.31 MHz and 7.52/12.65 KiB/s. Thus global
O2 is the fastest compute build, while targeted O2 preserves the compact native
image and essentially the control storage rate. A separate `_Bios()`/`_Bdos()`
IRAM trial produced no meaningful gain and was discarded.

The dispatchers are not the complete native CP/M service implementation.
RunCPM-owned disk/FCB helpers occupy about 2,893 bytes of text, platform
`_sys_*` file wrappers about 2,773 bytes, and directory-enumeration helpers
about 1,130 bytes. Including the two dispatchers gives roughly 9.3 KiB of
project-owned native BIOS/BDOS code before literal pools and the much larger
Arduino filesystem/SD driver implementation. Pulling that whole transitive
path into internal instruction SRAM would therefore cost considerably more
than moving just `_Bios()` and `_Bdos()`, and would still leave device waits.

## Underlying filesystem and SDMMC path

The current ESP32-S3 read path is approximately:

```text
guest BDOS call
  -> native _Bdos / _ReadSeq / _sys_readseq
  -> Arduino fs::File and VFSFileImpl
  -> ESP-IDF VFS FAT adapter
  -> FatFs f_read
  -> FatFs SDMMC disk bridge
  -> ESP-IDF sdmmc command and host transaction
  -> SDMMC DMA/peripheral and card
```

The equivalent write path uses the corresponding write functions. Every named
library layer in this path currently links at `0x420...`, the ESP32-S3 cached
flash window. Representative linked text-body sizes are:

| Layer | Read/open examples | Write examples |
| --- | --- | --- |
| Arduino FS | `File::read` 32 B, `File::seek` 32 B | `File::write` 32 B |
| VFS file object | `VFSFileImpl::read` 41 B, `seek` 37 B | `write` 43 B |
| VFS FAT adapter | `vfs_fat_open` 267 B, `read` 72 B, `lseek` 102 B | `write` 173 B |
| FatFs | `f_open` 527 B, `f_read` 379 B, `f_lseek` 361 B | `f_write` 439 B |
| FatFs SD bridge | `ff_disk_read` 29 B, `ff_sdmmc_read` 80 B | corresponding functions 29 B and 80 B |
| SDMMC command layer | `sdmmc_read_sectors` 146 B, DMA helper 297 B | 151 B and 421 B |
| SDMMC host | `sdmmc_host_do_transaction` 847 B | shared by both paths |

Sizes exclude literal pools and all transitive callees. They show that the
software path is fragmented across many small layers; marking a RunCPM wrapper
does not relocate any separately compiled callee. The lowest-level bulk data
transfer is performed by the SDMMC peripheral and DMA, rather than a software
loop moving every card bit. Moving the command setup and wait code into internal
instruction SRAM can remove flash misses, but cannot remove card latency.

There is a more significant architectural cost above the driver. Each current
128-byte `_sys_readseq()` and `_sys_readrand()` operation opens the host file,
seeks, reads and closes it. Each write calls `_sys_extendfile()`, which performs
an append open/optional extension/close, then performs another open/seek/write/
close. This preserves the present simple RunCPM abstraction, but may cost much
more than the 128-byte transfer. Measure persistent-file or grouped-record
handling before investing internal SRAM in deep SDMMC code; any such change
must preserve CP/M FCB, extent, concurrent-open and error semantics.

The selected SDK enables a FatFs per-file cache and prefers external RAM for
FatFs allocations. Consequently some filesystem working data may be in external
PSRAM and reached through the data cache. This is another item to measure during
a storage benchmark; it is not evidence by itself that relocation will help.

### Where CP/M file bytes live now

RunCPM does not preload a CP/M drive or its files into PSRAM. A sequential read
currently takes this data path:

```text
SD card -> SDMMC/FatFs buffers -> 128-byte _sys_readseq stack buffer
        -> selected DMA address inside the 64 KiB Z80 RAM[] array
```

The temporary 128-byte `dmabuf` is on the RunCPM task stack, which is allocated
from internal memory in this configuration. `RAM[]` is also internal data SRAM
at linked address `0x3FCA34F9`. When a COM file is loaded, its instructions end
up in this internal guest memory and all later opcode fetches come from there.
FatFs may allocate working buffers in external PSRAM, but this does not amount
to a PSRAM copy of the drive or of every open file.

### Optional selected-file virtual drive or cache

A future fast drive containing only selected utilities, VM modules or test
files is practical, but it is a storage feature rather than an instruction
placement attribute. Useful designs, from simplest to more capable, are:

1. A read-only embedded-file table in flash. This removes the SD-card dependency
   and filesystem traversal, while a launched COM file still copies into
   internal `RAM[]`. Reads of the embedded image itself use cached flash.
2. A selected-file image or RAM drive in external PSRAM. It consumes plentiful
   capacity, avoids card latency after population and benefits from the hardware
   data cache. It can be populated from embedded flash or from SD at startup.
3. A small record/block cache in internal data SRAM in front of SD or PSRAM.
   This is appropriate only for demonstrated hot records; a few KiB is more
   defensible than duplicating an entire drive in scarce internal SRAM.
4. A bounded cache of open host-file handles. It attacks the current open/seek/
   close cost without copying whole files, but requires careful CP/M FCB,
   rename/delete, close, extent and error handling.

Read-only selected files make the first two designs simple. A writable virtual
drive needs a persistence policy: volatile, explicit save, write-through or
write-back with flush and failure handling. For VMEX, the same pattern can hold
stable runtime modules in flash, a larger working set in PSRAM, and only active
bytecode/procedure pages in internal data SRAM.

## How code can be relocated

- Functions compiled from project source, including headers merged into the
  Arduino-generated sketch C++ file, can be marked `IRAM_ATTR` easily. Their
  literal pools move with them, as verified for `Z80run()`.
- A wrapper in internal instruction SRAM may call a function in cached flash,
  but the callee stays in flash. `IRAM_ATTR` is not transitive, and separately
  compiled virtual Arduino FS calls cannot be inlined into the wrapper.
- Arduino library sources such as `FS.cpp` can be forked into a project-owned
  library and annotated, but every important callee would still need separate
  treatment. Editing the installed Arduino package would be fragile because an
  update can overwrite it.
- ESP-IDF FatFs and SDMMC arrive in this Arduino core as precompiled archives.
  Their source functions cannot be annotated from the generated sketch C++.
  Selected function sections can technically be captured by a custom linker
  script, or the Arduino/ESP-IDF libraries can be rebuilt with component linker
  fragments. Both approaches are possible but materially more complex and less
  portable than marking project code.
- Relocating an entire archive/object is easier to describe to the linker but
  wastes internal SRAM on initialization, error and uncommon paths. Function
  section placement is preferable if a profile justifies library relocation.

For performance-only placement, an internal function is allowed to call flash
code. Code intended to run while the flash cache is disabled has a stronger
rule: every reachable instruction and referenced constant must remain in
internal memory or ROM. RunCPM does not need the filesystem path to operate
during cache-disabled intervals, so that stronger and much larger closure is
not a current requirement.

The practical order is therefore: measure and try `_Bdos()`/`_Bios()` plus
small project-owned console or memory helpers; measure the 128-byte record
workflow; improve excessive host-file operations if justified; only then
consider custom placement or rebuilding of FatFs/SDMMC functions. This same
rule applies to VMEX: native CPU-bound primitives are stronger internal-SRAM
candidates than host I/O stacks whose time is dominated by peripherals.

## Placement inventory and next experiments

The current link map gives a clear order for further work:

1. Benchmark the compile-only `-O2` and `-O3` candidates with `VTCPU` on the
   same board and card. This changes optimization before consuming IRAM for
   more subsystems. Also record runtime free and largest internal-memory blocks
   at boot, because the linker window does not include later allocations.
2. Try `_Bdos()` alone in IRAM. Its current `-Os` body and literal pool cost
   about 2.2 KiB. Measure a tight cheap-BDOS-call test and console throughput;
   `VTCPU` deliberately contains no timed BDOS calls and cannot show this gain.
3. Add `_Bios()` only if a BIOS-heavy workload justifies its roughly 0.6 KiB.
   Normal RunCPM programs use native BDOS much more often.
4. If character output is CPU-bound, profile `_putcon()` -> `_putch()` ->
   `virtualWrite()` -> `ByteQueue::push()`. Moving only the dispatcher does not
   move those separately compiled callees. The current active/quiet `VTCPU`
   equality already shows that terminal flash-cache competition no longer
   slows a compute-only guest after `Z80run()` moved to IRAM.
5. Instrument `_sys_readseq()` and `_sys_writeseq()` into open, seek, transfer,
   flush and close time. At 14.5 KiB/s read and 8.9 KiB/s write, each 128-byte
   record currently costs about 8.6 ms and 14.0 ms. Those delays are far larger
   than a likely flash instruction-cache miss.
6. Test a bounded persistent host-file handle or sequential-record cache after
   the timing split. The present read opens, seeks and closes for every record;
   the present write can open/extend/close and then open/seek/write/close. This
   is a stronger candidate than relocating the precompiled SDMMC stack.
7. Consider direct reads into `_RamSysAddr(dmaAddr)` to avoid the temporary
   128-byte buffer and copy. Preserve CP/M's `0x1a` fill semantics for a short
   final record.

The ESP-IDF and FreeRTOS routines already placed in IRAM are there for flash-off
and interrupt safety and should remain untouched. A project `IRAM_ATTR` is not
transitive: moving `_Bdos()` leaves its filesystem, queue and display callees in
cached flash. Every additional IRAM byte also removes roughly one byte from the
internal data/heap pool, so each placement experiment must record map size,
runtime heap, speed and functional tests together.

## How hot the native CP/M paths are

RunCPM does not execute a guest CP/M 2.2 BDOS. `INT_HANDOFF` transfers an
emulated RST 08h/10h synchronously to native `_Bios()`/`_Bdos()`. External COM
programs execute as Z80 code between those calls.

The complete current 64 KiB Z80 address space is the `RAM[]` array in internal
data SRAM. It contains loaded COM instructions, application data and the tiny
BIOS/BDOS jump/RST/RET handoff stubs. The LX7 does not execute those guest bytes
through its instruction cache: `Z80run()` fetches them as data. Thus all current
interpreted Z80 code already has deterministic internal-SRAM access; there is
no lower-level guest CP/M BIOS/BDOS body waiting in flash or PSRAM to be cached.
Larger future banked guest configurations would require a new placement choice.

- BDOS function 2 enters `_Bdos()` for every character emitted by many CP/M
  programs. Function 9 enters it once and performs the complete `$`-terminated
  string loop natively. Console status and input calls can also be frequent.
- Sequential and random file calls enter `_Bdos()` per CP/M record, then run
  native FCB/path helpers and filesystem code. The native implementation is a
  major semantic acceleration over emulating a complete BDOS, but SD latency
  can dominate these calls.
- `_Bios()` is smaller and usually less active because the native BDOS handles
  most normal CP/M application services. Its console and memory-transfer cases
  can still be hot in workloads that call BIOS directly.
- The internal CCP is native too, but it is mostly active at the command prompt
  and while loading a program. It is not on the compute path of a running COM
  application and is a low-priority internal-SRAM candidate.

`VTCPU` measures an uninterrupted guest instruction loop, so it proves the
benefit of placing `Z80run()` in internal instruction SRAM but says nothing
about BIOS/BDOS placement. A BIOS/BDOS experiment needs separate workloads:

1. repeated cheap BDOS calls to measure handoff and dispatcher cost;
2. console output with terminal active and with output consumption controlled;
3. sequential 128-byte file reads/writes to distinguish native processing from
   storage time;
4. unchanged functional tests plus linker-map and internal-heap comparison.

Change one placement group at a time. The first reasonable experiment is the
small `_Bdos()` dispatcher, optionally `_Bios()` in the same build. If it helps
console-heavy or service-heavy applications, add only the small console and
memory helpers shown by profiling. Keep filesystem and SD stacks in flash until
a CPU-bound storage benchmark demonstrates otherwise.

## Applying this to VMEX

Before porting VMEX, identify and measure these groups independently:

- bytecode/token fetch, decode and dispatch;
- operand/value stack access, tag checks and common arithmetic;
- call/return, frame and local-variable access;
- the most frequent native primitives and runtime-service dispatcher;
- allocation, garbage collection and object/string traversal loops;
- host I/O, terminal rendering and storage.

Place the continuous interpreter loop and its inlined fast paths in internal
instruction SRAM. Keep the active VM stack, registers, dispatch metadata and
other repeatedly touched state in internal data SRAM. Keep the active VMEX
bytecode/source working set there too when its size permits. For a larger image,
start with the hardware-cached PSRAM mapping and measure whether a small
software-managed hot-page or hot-procedure cache is justified. Large object
heaps, source images, framebuffers and cold tables can use external PSRAM, with
layouts that favor cache-line locality. Promote native primitives individually
when profiles show that they consume CPU time; do not place the entire runtime
or all host drivers internally merely because they are part of the VM.

FreeRTOS task affinity does not reserve all hardware activity on a core, but it
does keep each application task from migrating. Internal placement makes the
critical interpreter path independent of flash instruction-cache displacement
by the terminal task. The current S3 task, cache and memory details are recorded
in [ESP32-S3 RunCPM and terminal integration](260909-13 runcpm-s3-dual-task.md).
