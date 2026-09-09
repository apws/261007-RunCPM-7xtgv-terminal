# RunCPM dual-core Arduino port plan

Saved 2026-09-08 from the planning discussion. Read the
[user addendum](runcpm-port-addendum.md) first: it narrows the refactoring scope
and defines the resource-conscious working approach.

## Goal and architecture

Extend the existing `7xtgv-vt100-runcpm` Arduino project with board profiles:
RP2350 remains the reference; port first to the 3.5-inch ESP32-S3, then the
5-inch ESP32-S3, and finally the 10-inch ESP32-P4.

Preserve the virtual serial connection: one core owns RunCPM and storage;
the other owns terminal parsing, input, rendering and PC mirroring. Reuse the
bounded queues, acquire/release publication, readiness handshake and output
backpressure. CP/M must not directly manipulate terminal/display state.

Keep the interpreter, native BIOS/BDOS/internal CCP, fonts, palette and current
terminal behavior shared. Retain Arduino and one project with target profiles.
No emulator optimization, JIT, new terminal protocol or complete implementation
of future VT/7xtgv extensions is required by this port.

For later measured placement of the interpreter and native services, including
the architectural correspondence with VMEX, see
[Interpreter porting and internal-SRAM policy](interpreter-porting-memory.md).

## Target facts and checks

| Target | Application CPU | Planned display at 6×10 |
| --- | --- | --- |
| 2-inch RP2350 | Cortex-M33 pair; current profile `arch=arm`, `os=none`, 150 MHz | Existing 80×24 terminal with 52×24 TFT viewport |
| 3.5-inch ESP32-S3 | Dual Xtensa LX7 | 480×320 landscape, 80×32 cells |
| 5-inch ESP32-S3 | Dual Xtensa LX7 | 800×480, 133×48 cells physically available |
| 10-inch ESP32-P4 | Dual HP RISC-V, RV32IMAFC plus extensions | 1280×800, 213×80 cells physically available |

All selected application toolchains use little-endian data. Confirm integer
widths and queue atomic support when compiling each target; preserve explicit
guest byte/word accesses and inspect alignment/address-wrapping assumptions.
Hardware floating point does not automatically accelerate floating-point code
inside emulated CP/M applications.

RP2350's alternative Hazard3 is RV32IMAC plus extensions and remains an optional
later experiment. Our existing firmware uses M33. Earlier conversation asserted
that mixed M33/Hazard3 operation is impossible; do not rely on that assertion:
the datasheet describes architecture selection per processor socket. Exact SDK
support for mixed operation is outside this port's scope.

P4 CPU extensions and supported frequency depend on silicon revision. Record
our board revision and actual build flags before assuming 400 MHz or Zb support.
The P4 low-power core is not used for either application task.

References: [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf),
[ESP32-S3 overview](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/product-overview.html),
[P4 datasheet](https://documentation.espressif.com/esp32-p4_datasheet_en.html),
[P4 revision 1.3 datasheet](https://documentation.espressif.com/esp32-p4-chip-revision-v1.3_datasheet_en.html).

## Incremental implementation

1. Identify the downloaded RunCPM baseline and read its actual porting guidance
   and existing ESP32 abstractions before introducing new interfaces. This
   upstream review remains outstanding; local source inspection alone did not
   establish S3/P4 upstream support or that we would be the first port.
2. Isolate only the necessary target differences in flat board headers and
   small helpers: startup/core assignment, storage setup, display configuration
   and serial selection. Preserve RP2350 behavior and compile/test it after
   touching shared code.
3. Inspect the 3.5-inch vendor SD example/schematic first. Prefer the existing
   SD filesystem path when compatible. A fixed flash/PSRAM file set was accepted
   as a fallback for initial proof, not as mandatory new infrastructure.
   RunCPM normally maps host files/directories, not a raw CP/M sector image.
4. Prove S3 RunCPM via serial with minimal display activity, then connect the
   existing terminal using pinned FreeRTOS tasks on opposite cores. Keep RTOS
   startup and cooperation in a small platform adapter. Test long compute-only
   workloads for idle-task/watchdog starvation, as well as queue waits.
5. Reuse the verified 3.5-inch native-panel/rotated-canvas configuration. Flush
   changed batches appropriately; avoid full framebuffer transfers per glyph.
   Generalize the terminal's fixed dimensions and RP2350-only indicator only
   as needed for 80×32. Audit mirror buffer bounds when later expanding columns.
6. Reuse the S3 work for the 5-inch board; then add P4 startup, SD/SDMMC and
   MIPI framebuffer integration using the working demos and existing P4 notes.
   Check internal RAM budget for guest RAM, task stacks and queues; keep large
   display buffers in the appropriate PSRAM/DMA allocation.

## Acceptance and checkpoints

- Preserve the working RP2350 firmware/build as rollback. Compile only the
  target under change, plus RP2350 when shared code changes warrant it.
- Test storage initialization, directory listing, create/read/write/rename/delete
  and file round-trip equality in a dedicated test area before application use.
  Report bus setup separately from card/filesystem mount status.
- Boot CCP; run DIR, MBASIC arithmetic and VTESTS; exercise BBC BASIC and Turbo
  Pascal when writable storage is ready. Keep test names VT-prefixed and mirror
  files under `cpmdrive/`.
- Verify 6×10 orientation, colors, wrapping, scrolling, keyboard input, PC mirror,
  serial reconnect and TFT operation without a connected PC. Request Petr's
  visual confirmation at each meaningful display milestone.
- Test sustained output and compute-only execution without hangs, queue loss or
  watchdog resets. Record task affinity, clock, build settings and storage path.
- Measure performance only after correctness, using existing quiet application
  workloads. The old approximately 27 MHz estimate is workload-specific.
- Stop at small reviewable milestones; document confirmed results and remaining
  questions. Port completion is allowed to span several sessions or days.
