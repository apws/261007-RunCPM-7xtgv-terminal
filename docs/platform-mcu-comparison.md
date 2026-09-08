# Display platform and CPU comparison

The four tested targets do not share one processor family. The display
controller and the CPU that runs Arduino code are separate concerns: the
display demos use the same 7xtgv software idea, but their board support and
RunCPM porting requirements differ.

| Target | Display/controller path | MCU and CPU | Current status |
| --- | --- | --- | --- |
| 2-inch Waveshare | 320×240 ST7789 over SPI | **RP2350**, two Cortex-M33 cores in the selected build | Full VT100/VT52 + RunCPM target |
| 3.5-inch JC8048W535 | 320×480 AXS15231B over QSPI | **ESP32-S3**, two Xtensa LX7 cores, up to 240 MHz | 7xtgv demos verified; RunCPM not ported |
| 5-inch JC8048W550 | 800×480 ST7262 over RGB parallel | **ESP32-S3**, two Xtensa LX7 cores, up to 240 MHz | 7xtgv demos verified; RunCPM not ported |
| 10-inch JC8012P4A1C | 1280×800 JD9365 over two-lane MIPI DSI | **ESP32-P4**, dual RISC-V cores, up to 400 MHz | 7xtgv demos verified; RunCPM port is future work |

## RP2350 core selection

RP2350 silicon provides two selectable CPU architectures: dual Arm
Cortex-M33, or dual Hazard3 RISC-V. This is a boot/configuration choice for
the processor image; the board does not run one M33 core and one Hazard3 core
at the same time. Our Arduino-Pico profiles explicitly use `arch=arm`, and
the compiled RunCPM firmware therefore runs on the Cortex-M33 pair. The
Hazard3 option has not been used or tested here.

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
