# Display platform and CPU comparison

The four tested targets do not share one processor family. The display
controller and the CPU that runs Arduino code are separate concerns: the
display demos use the same 7xtgv software idea, but their board support and
RunCPM porting requirements differ.

| Target | Display/controller path | MCU and CPU | Current status |
| --- | --- | --- | --- |
| 2-inch Waveshare | 320×240 ST7789 over SPI | **RP2350**, two Cortex-M33 cores in the selected build | Full VT100/VT52 + RunCPM target |
| 3.5-inch JC8048W535 | 320×480 AXS15231B over QSPI | **ESP32-S3**, two Xtensa LX7 cores, 240 MHz in this build | Dual-task RunCPM/terminal verified |
| 5-inch JC8048W550 | 800×480 ST7262 over RGB parallel | **ESP32-S3**, two Xtensa LX7 cores, up to 240 MHz | 7xtgv demos verified; RunCPM not ported |
| 10-inch JC8012P4A1C | 1280×800 JD9365 over two-lane MIPI DSI | **ESP32-P4**, dual RISC-V cores, up to 400 MHz | 7xtgv demos verified; RunCPM port is future work |

## RP2350 core selection

RP2350 silicon provides two selectable CPU architectures: dual Arm
Cortex-M33, or dual Hazard3 RISC-V. This is a boot/configuration choice for
the processor image; the board does not run one M33 core and one Hazard3 core
at the same time. Our Arduino-Pico profiles explicitly use `arch=arm`, and
the compiled RunCPM firmware therefore runs on the Cortex-M33 pair. The
Hazard3 option has not been used or tested here.

## RP2350 versus ESP32-S3 memory path

| Property | RP2350 reference target | ESP32-S3 JC8048W535 |
| --- | --- | --- |
| Application runtime | Arduino-Pico with `os=none`; `setup()` and `setup1()` run directly on the two M33 cores | Arduino-ESP32 above FreeRTOS; RunCPM and terminal are priority-1 tasks pinned to opposite LX7 cores |
| Clock used here | 150 MHz | 240 MHz |
| On-chip SRAM | 520 KiB in ten banks; ordinary code and data can be linked into it | 512 KiB shared among instruction RAM, data RAM, caches and system use; the sketch linker exposes a 320 KiB static-data ceiling |
| Normal program code | 16 MB external QSPI flash through the chip-level 16 KiB XIP cache | External flash through the configured 16 KiB instruction-cache path |
| External RAM | This board/profile selects `psram=0mb`; all RunCPM state is on-chip | 8 MB OPI PSRAM through the external-memory/data-cache path |
| Main large object | No full-screen external-RAM canvas | 307,200-byte rotated RGB565 canvas in PSRAM |
| Z80 guest RAM | 64 KiB on-chip SRAM | 64 KiB on-chip data SRAM |
| `Z80run()` now | 27,684-byte Arm body in cached XIP flash | 28,802-byte Xtensa body explicitly placed in on-chip instruction SRAM |
| Current whole-build static use | 130,852 B of 524,288 B reported by the linker | 141,448 B of 327,680 B reported by the linker; canvas excluded because it is dynamic PSRAM |

RP2350's QMI can also map external PSRAM on a second memory-select and serve it
through the same 16 KiB XIP cache, including cached writes. That is an optional
silicon capability, not a path used by the current 2-inch board profile. Its
flash cache supports line pinning, but moving selected functions into ordinary
on-chip SRAM is simpler when enough of the 520 KiB remains.

The complete RP2350 `Z80run()` body is larger than its XIP cache, but one CP/M
workload executes only a subset of opcode handlers repeatedly. Its effective
hot instruction footprint can therefore fit much better than the linked
function size suggests. The S3 terminal and PSRAM canvas also use the external
memory subsystem; before `Z80run()` was moved to instruction SRAM, that traffic
reduced the active CPU result from 17.02 MHz quiet to 10.79 MHz active. Internal
placement made both about 17.21 MHz.

The earlier roughly 27 MHz RP2350 result used its old startup workload, so it
is evidence that the 150 MHz M33 build is efficient, but not a controlled
comparison with S3 `VTCPU`. Native instruction selection, indirect-branch cost,
code layout and the active opcode set matter more here than host clock alone.
Run the same `VTCPU` binary and firmware protocol on both boards before drawing
a numeric architecture ratio.

A general CP/M RAM-throughput command is not needed for the current port:
`VTCPU` already exercises the interpreter and on-chip guest RAM, and `VTDRV`
exercises the filesystem path. Add a focused `VTBLK` later if block-opcode
handling (`LDIR/LDDR/CPIR`) or guest-memory placement becomes the actual
question.

The current RunCPM sketch also uses RP2350/Arduino-Pico facilities such as
`setup1()`/`loop1()`, the RP2350 USB serial device, and the RP2350 SD/SPI
layout. Those are board/runtime dependencies, separate from the portable CP/M
interpreter.

## What this means for RunCPM

RunCPM is ordinary C/C++ and is not inherently tied to Arm. Its Z80/8080
interpreter could be compiled for Xtensa or RISC-V, but a complete port needs
new board integration:

- replace RP2350 multicore entry points with an ESP-IDF/Arduino FreeRTOS task
  or equivalent;
- provide the target's serial/USB input and output backend;
- adapt SD storage (SPI on the RP2350 board versus the target board's actual
  SPI or SDMMC wiring);
- provide the display/framebuffer backend and geometry;
- check compiler attributes, timing assumptions, and any direct RP2350 API
  calls.

The CP/M disk image layout, terminal parser, fonts, palette definitions, and
the core emulation logic should remain reusable. A port to ESP32-S3 would
target Xtensa-specific compiler output; a port to ESP32-P4 would target
RISC-V output. Neither would be cycle-compatible with the existing RP2350
build without deliberate timing work, although the current RunCPM mode is
already a maximum-speed interpreter rather than a cycle-accurate emulator.

The 10-inch-specific port checklist is tracked in
[`jc8012p4a1c-runcpm-port-notes.md`](jc8012p4a1c-runcpm-port-notes.md).
