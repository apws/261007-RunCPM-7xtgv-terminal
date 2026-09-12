# 7xtgv ANSI xterm-256 GCLS and Mandelbrot plot plan

Date: 2026-09-12

Status: implemented in the terminal source on 2026-09-12; host parser,
keyboard-helper and direct-display runtime tests pass. ESP32-S3 and RP2350
builds pass. Hardware validation of `BROT-BS` and `BROT-PS` remains pending.

## Summary

Implement xterm-256 plotting as a one-shot graphics submode inside the existing
ANSI/VT100 personality.

The physical Arduino_GFX surface acts as a temporary graphics backing. Plot
samples bypass the 7xtgv character/attribute buffers; ordinary text continues
through those buffers and remains available to the PC mirror.

## Controls and state

Reserve the private 7xtgv sequence `CSI > 4 v` (`ESC[>4v`) as **GCLS**.
Recognize GCLS from both the local keyboard decoder and guest terminal output.

Controls:

```text
Ctrl+] g                 GCLS
Shift+Alt+Backspace      GCLS
Ctrl+] 1                 ANSI/VT100; cancel unfinished plotting
Ctrl+] 2                 VT52; cancel unfinished plotting
Ctrl+] r                 full terminal repaint; destroy temporary plot
```

Both GCLS keyboard shortcuts generate `ESC[>4v`. The firmware consumes this
private control locally; it is not sent to the running CP/M application.

GCLS:

1. selects ANSI/VT100;
2. clears the physical Arduino_GFX client rectangle to white;
3. resets plot coordinates and scanline state;
4. clears pending TFT cell-redraw flags without changing the logical terminal
   or PC-mirror state;
5. arms one xterm-256 plot transaction.

GCLS alone does not suppress ordinary TFT terminal-cell painting. Suppression
starts with the first valid xterm sample, so a user can still see and enter the
CP/M command after manually invoking GCLS.

Use explicit internal states:

```text
ANSI_TEXT
ANSI_PLOT_READY
ANSI_PLOT_ACTIVE
ANSI_PLOT_SAMPLE_ARMED
```

`CSI 0m` flushes the final scanline, resets SGR, ends plot capture
automatically, and resumes ordinary opaque terminal-cell rendering. The
terminal personality remains ANSI/VT100.

## Parsing and rendering

In `ANSI_PLOT_READY`, ordinary output remains normal text until the first valid
`CSI 38;5;n m`.

A valid `CSI 38;5;n m`, with `n` from 0 through 255, converts the index directly
to RGB565 and arms exactly one sample. The next printable byte, including a
space, Phillip Stevens' `#`, or Bill Shen's RunCPM-masked `[`, is consumed as
one pixel and never enters the terminal buffers or PC mirror.

While a plot transaction is active:

- CR is consumed by the plot stream;
- LF flushes the current RGB565 scanline, resets X, and advances Y;
- the first completed plot row also contributes one CR/LF to the logical text
  terminal, separating the welcome and completion messages without scrolling
  it for every graphics row;
- malformed or out-of-range extended colors do not arm a sample;
- a control sequence received while a sample is armed cancels that incomplete
  sample safely.

`CSI 48;5;n m` updates the plot background-color state but does not arm a
sample. `CSI 39m` and `CSI 49m` restore plot defaults. Outside a GCLS
transaction, extended-color sequences retain the current safe behavior and do
not create exact-color terminal cells.

Use the conventional xterm palette independently of the 7xtgv palette:

- indexes 0–15: conventional ANSI/xterm base colors;
- indexes 16–231: 6x6x6 cube using `0, 95, 135, 175, 215, 255`;
- indexes 232–255: grayscale `8 + 10 * (index - 232)`;
- convert once per SGR to RGB565.

Buffer one logical RGB565 scanline sized from the **current Arduino_GFX client
width**. On the current ESP32-S3 module this is 480 pixels. Do not impose a
fixed 320-pixel buffer. Allocate or size the scanline after the display has
reported its post-rotation width, and clip drawing to the current client
rectangle.

Flush each completed row with one Arduino_GFX bitmap/span operation. Mark the
existing buffered-canvas flush as pending where required.

The first renderer uses 1x1 samples at the client's top-left. Keep sample width
and height as renderer parameters so later 1x2, 2x3, 3x5, and 6x10 views do not
require parser changes.

## Layering rules

- GCLS does not clear or rewrite `charRAM`, `attrRAM`, history, profiles, or
  PC-mirror state.
- Plot pixels exist only in the Arduino_GFX canvas, controller GRAM, or platform
  framebuffer.
- Ordinary text produced during plotting continues updating the logical
  terminal and mirror, while TFT cell painting waits until plot capture ends.
- At `CSI 0m`, discard deferred TFT dirty cells accumulated during capture;
  text emitted afterward paints its normal opaque 6x10 INK/PAPER rectangles
  over the completed image.
- Later terminal output, erasure, scrolling, mode changes, and refresh
  progressively or completely overwrite the plot.
- Erased terminal cells cannot reveal previous plot pixels; this milestone does
  not retain an independently composited graphics layer.
- Do not mirror graphics to the PC.
- Do not implement cell transparency, exact xterm colors in ordinary terminal
  cells, taskbar layout, bird's-eye rendering, or zoom controls in this phase.

## Tests

- Add parser tests for complete and fragmented `38;5`, `48;5`, `39`, `49`, and
  `0m` sequences, including indexes 0, 15, 16, 21, 46, 51, 196, 201, 226, 231,
  232, and 255.
- Verify malformed, incomplete, and out-of-range sequences never consume the
  following ordinary character as a plot sample.
- Verify `#` and `[` produce identical plot samples and never enter terminal
  buffers.
- Verify CR/LF produces exactly one graphics-row advance and only one logical
  status-line advance for the complete transaction.
- Verify GCLS leaves logical buffers intact, suppresses pending TFT repaint,
  clears white, and resets plot position.
- Verify `SGR 0`, `Ctrl+] 1`, and `Ctrl+] 2` recover cleanly from active or
  half-armed plotting.
- Extend helper tests for `Ctrl+] g` and keyboard-decoder tests for `ESC[>4v`.
- Retest existing `Ctrl+] 1` ANSI and `Ctrl+] 2` VT52 behavior before hardware
  plotting.

Hardware test on the ESP32-S3 TFT:

1. GCLS produces a white physical canvas.
2. Unchanged `BROT-BS` renders despite its `0xDB` becoming `[`. 
3. Unchanged `BROT-PS` renders from `#`.
4. The PC mirror contains status text without pixel characters or graphics
   rows.
5. Completion text appears after `SGR 0`.
6. Normal output and `Ctrl+] r` overwrite the temporary image predictably.

## Assumptions and boundaries

- The current 80x32 ESP32-S3 layout remains unchanged.
- The plot is approximately 257x161 samples and fits the current 480x320
  client at 1x1.
- White RGB565 `0xffff` is the initial graphics background.
- GCLS is a terminal-local private control, not a new terminal personality and
  not a standard xterm command.
- `docs/260912-01_7xtgv_xterm256_brot_plan.md` and
  `docs/260912-10_7xtgv_terminal_viewport_birdseye_xterm256_spec.md` remain
  background designs. This document is authoritative for the first
  implementation.
