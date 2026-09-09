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

These figures come from the linked IRAM build
`build/build-vt100-runcpm-s3-iram/7xtgv-vt100-runcpm.ino.elf` made on
2026-09-09. They are linked Xtensa code sizes, not source sizes.

| Native function | Text body | Literal pool | Effective placement size |
| --- | ---: | ---: | ---: |
| `Z80run()` | 28,698 B | about 198 B including alignment | 28,896 B |
| `_Bios()` | 547 B | 36 B | 583 B |
| `_Bdos()` | 2,182 B | 40 B | 2,222 B |
| BIOS + BDOS dispatchers | 2,729 B | 76 B | 2,805 B |

The BIOS and BDOS dispatchers together are only about 9.7% of the internal-SRAM
cost already accepted for `Z80run()`. They currently remain in cached flash.

The dispatchers are not the complete native CP/M service implementation.
RunCPM-owned disk/FCB helpers occupy about 2,893 bytes of text, platform
`_sys_*` file wrappers about 2,773 bytes, and directory-enumeration helpers
about 1,130 bytes. Including the two dispatchers gives roughly 9.3 KiB of
project-owned native BIOS/BDOS code before literal pools and the much larger
Arduino filesystem/SD driver implementation. Pulling that whole transitive
path into internal instruction SRAM would therefore cost considerably more
than moving just `_Bios()` and `_Bdos()`, and would still leave device waits.

## How hot the native CP/M paths are

RunCPM does not execute a guest CP/M 2.2 BDOS. `INT_HANDOFF` transfers an
emulated RST 08h/10h synchronously to native `_Bios()`/`_Bdos()`. External COM
programs execute as Z80 code between those calls.

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

`VTMHZ` measures an uninterrupted guest instruction loop, so it proves the
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
other repeatedly touched state in internal data SRAM. Large object heaps,
source images, framebuffers and cold tables can use external PSRAM, with layouts
that favor cache-line locality. Promote native primitives individually when
profiles show that they consume CPU time; do not place the entire runtime or
all host drivers internally merely because they are part of the VM.

FreeRTOS task affinity does not reserve all hardware activity on a core, but it
does keep each application task from migrating. Internal placement makes the
critical interpreter path independent of flash instruction-cache displacement
by the terminal task. The current S3 task, cache and memory details are recorded
in [ESP32-S3 RunCPM and terminal integration](runcpm-s3-dual-task.md).
