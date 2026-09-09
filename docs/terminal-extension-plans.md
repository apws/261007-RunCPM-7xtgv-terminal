# Terminal extension plans

Roadmap for the 7xtgv terminal layer. Items identified as compatibility targets
are intended final capabilities; optional profiles remain subjects for later
investigation.

## Terminal modes and compatibility targets

The standards-compatible terminal will support:

- The complete VT52 text-terminal command set.
- The full VT100 text-terminal command set, including terminal
  modes, cursor control, editing, scrolling regions, tab stops, character sets,
  reports and replies.
- Full ANSI SGR parsing for supported text attributes and colors. Color SGR is
  an ANSI/xterm compatibility layer beyond the original monochrome VT100.
- xterm 16-color and 256-color SGR, including the indexed-color forms used by
  the planned pixel-stream display mode.

Implement standard scrolling margins and text-region operations. This permits
software to scroll a terminal window or subsection without redrawing the whole
screen. Use cell-buffer moves, TFT controller scrolling, direct framebuffer
access, dirty rectangles or another fast path as appropriate for each display;
the internal implementation does not need to imitate terminal hardware.

Text-only VT340 compatibility and a period-appropriate WYSE color-terminal
profile are optional extensions. Select their exact command sets from original
specifications before implementation. They are intended to improve compatibility
with existing software; VT340 graphics protocols are outside the initial text
target.

The native 7xtgv mode is separate from all terminal emulation. It must implement
only the defined 7xtgv virtual-chip specification, with strict command, memory,
palette and timing semantics. Do not infer its behavior from VT52, VT100, xterm,
VT340 or WYSE behavior. Standard terminal modes may use the 7xtgv renderer as a
backend, but they must not silently extend the native 7xtgv interface.

Screen geometry must be configurable for the planned panels. With the current
6x10 cell these provide approximately:

| Panel | Native text grid |
|---|---:|
| 320x240 | 53x24 cells, subject to the selected viewport width |
| 480x320 | 80x32 cells |
| 800x480 | 133x48 cells |
| 1280x800 | 213x80 cells |

## Command history

Add a small input-line history layer shared by USB serial, a future local
keyboard scanner, UART, SPI, and I2C input sources.

The terminal cannot reliably distinguish the CCP prompt from MBASIC, BBC BASIC,
Turbo Pascal, an editor, or another interactive program. Do not infer this from
screen text. Use an explicitly selected history bank instead.

### History lists

- Provide ten independent numbered lists `#0` through `#9`, selected by the
  matching `Alt+0` through `Alt+9` key.
- Exactly one list is active at a time. Only that list records Enter-completed
  lines and answers Up/Down recall; never switch lists automatically.
- Each list is a fixed ring holding the latest 100 lines of at most 127 bytes.
  New lines overwrite the oldest after the list becomes full.
- Suppress empty and one-character entries, plus consecutive duplicates. Keep history in RAM first;
  optional SD persistence can be added later with an explicit save operation.
- Clear the current row, show `#n bankname` for one second, then clear that row
  again. A new bank selection redraws the label immediately and restarts the
  timeout.

### Bank keyboard profiles

Each bank also owns a configurable application name and plain/Ctrl/Alt mappings
for the arrows and six-key block. Host and future local keyboards send logical
private key events; the firmware translates them using the selected bank. The
boot-time `A/0/7XTGVTRM.INI` file defines the profiles; compiled defaults remain
available. See `terminal-application-profiles.md`.

### Input and recall path

Place history after physical-key decoding and before the existing CP/M input
queue. Capture bytes typed by the user until CR, excluding private history
controls. Recall must enqueue the stored bytes through that same input queue;
never draw recalled text directly into the screen buffer. An application such
as MBASIC then sees normal keyboard input and performs its own echo and editing.

Collect printable input bytes until CR and store that completed line without
the terminating CR. Handle ordinary Backspace by removing the last collected
byte; other cursor/edit controls need not be reconstructed. Recalling a line
injects its printable contents, and the user submits it with Enter normally.

Expose the functions through private input sequences consumed by 7xtgv and
never passed to CP/M. The PC mappings are:

| Host key | Local action |
|---|---|
| `Up` / `Down` | Previous/next entry in the selected bank |
| `Alt+0` ... `Alt+9` | Select history list `#0` ... `#9` |
| `Alt+PageUp` / `Alt+PageDown` | Select previous/next history list |
| `Alt+Up` / `Alt+Down` | Pass cursor Up/Down to the CP/M application |

Kitty, Konsole, the Python bridge and a future local keyboard scanner can all
generate the same private sequences. Plain Up/Down intentionally favors history;
Alt+Up/Down provides cursor keys to applications that need them.

### Application-independent boundary

Recall requires an empty application input line. This is usable inside MBASIC
and other REPLs: select their bank, press `Alt+Up`, then press Enter normally.
The terminal does not replace partly typed text and does not model an
application's cursor editing. On Enter it closes and stores the current
collected line, then starts collecting a new one. This avoids assumptions about
whether the program uses BDOS buffered input, reads individual console
characters, or implements a private editor.

### Implementation order

1. Add the ten fixed 100-line rings, explicit list selection, completed-line
   capture, recall and unit tests for duplicate suppression, wraparound and
   injection.
2. Add Kitty/Konsole mappings and Python-prefix commands, then test separate
   lists in CCP and MBASIC on hardware.

## xterm 256-color SGR

Support the widely implemented xterm indexed-color extensions:

- `CSI 38;5;n m`: foreground palette index `n`.
- `CSI 48;5;n m`: background palette index `n`.
- Decode indexes 0–15 as terminal colors, 16–231 as the 6×6×6 RGB cube, and
  232–255 as the grayscale ramp.
- In normal text mode, map the resulting RGB value to the nearest existing
  7xtgv ink or paper color. Preserve current 16-color behavior and attributes.
- Add parser tests for complete, fragmented, malformed, and out-of-range SGR.

## Optional pixel-stream display mode

Provide a private VT107 mode for compatibility with text-cell graphics streams
such as the Bill Shen and Phillip Stevens Mandelbrot benchmark.

- While enabled, each printable character represents one physical TFT pixel in
  the current xterm foreground color; escape sequences do not advance position.
- CR ends the current pixel row and LF advances to the next row. Define a clear
  policy for CR/LF pairs so each pair advances exactly once.
- Convert xterm palette entries directly to RGB565. Character identity is
  ignored, allowing `#`, CP437 block output masked to `[`, or another printable
  marker to produce the same pixel.
- Buffer up to one 320-pixel RGB565 scanline and write it to the TFT as one SPI
  rectangle. Bypass the 80×24 cell buffers and 6×10 glyph rendering.
- Default to a centered 1:1 image within 320×240; clip safely beyond either
  physical edge. A reset or explicit private command exits pixel mode and
  restores the normal terminal screen.
- Enter the mode only through an explicit private control or local command;
  never auto-detect output patterns.
- Keep the PC mirror usable: either forward equivalent xterm text output or show
  a concise pixel-mode status while the TFT owns the raster presentation.

The Mandelbrot programs still send roughly 396,300 bytes because they emit a
new color sequence for every point. Pixel mode improves TFT rendering by
flushing complete scanlines; it does not remove guest-side BDOS or serial/queue
traffic. A future general graphics protocol should use a more compact encoding
rather than treating this benchmark format as the primary graphics API.

Validation should cover the approximately 257×161 Mandelbrot output, correct
xterm palette conversion, clipping, reset recovery, queue responsiveness, and
normal ANSI/VT52 operation after leaving pixel mode.
