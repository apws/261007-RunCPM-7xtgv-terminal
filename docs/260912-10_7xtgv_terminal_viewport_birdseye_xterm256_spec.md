# 7xtgv terminal viewport, bird's-eye zoom-out, taskbar, and xterm-256 plot mode

**Working specification / implementation plan**  
**Date:** 2026-09-12  
**Scope:** current single active terminal implementation, with future multiwindow/vmex use kept in mind but **not implemented now**.

---

## 1. Purpose and design boundary

This document specifies a small set of rendering and viewport extensions for the current `7xtgv-terminal` implementation:

1. a fixed logical **80x24 compatibility terminal client area** for VT100/ANSI and VT52;
2. a fixed **system taskbar/status area** outside the scrolling terminal client on displays which have spare physical pixels, especially the baseline 480x320 TFT;
3. several **bird's-eye zoom-out views** of the same logical terminal surface;
4. minimal **xterm 256-color SGR** support in the existing VT100/ANSI CSI parser;
5. an experimental **xterm-256 Mandelbrot plot interpretation** selectable while staying inside VT100/ANSI mode;
6. clipping and panning rules which work on smaller TFTs without changing the logical terminal state.

The key architectural rule is:

> **Terminal interpretation, logical terminal state, viewport scale, and physical display layout are independent.**

Zooming out must never change the terminal's logical dimensions, cursor coordinates, scrolling model, CP/M-visible terminal behavior, or application output stream.

The current work remains a **single active terminal surface**. Future multiple windows/terminal instances on a larger 7xtgv/vmex desktop are explicitly out of scope here.

---

## 2. Terminology

Use the following terms consistently:

- **terminal client area**: the logical terminal content region, normally 80x24 cells for CP/M compatibility;
- **window frame / chrome**: decoration immediately around a future window/client area;
- **margin**: free desktop space outside/between windows;
- **padding**: space inside a frame before content; not normally needed for the terminal client itself;
- **taskbar / system band**: fixed 7xtgv UI outside the scrolling terminal client;
- **viewport**: the currently visible physical portion of the logical terminal/desktop;
- **bird's-eye view**: reduced cell representation which intentionally does **not** render readable text;
- **full view**: normal 6x10 physical cell rendering with real font/glyph lookup;
- **render scale**: physical rectangle assigned to one logical cell/sample: 6x10, 3x5, 2x3, 1x2, or 1x1;
- **terminal personality**: VT100/ANSI, VT52, or native 7xtgv;
- **xterm-256 plot mode**: an alternate rendering interpretation inside VT100/ANSI, not a separate terminal parser.

---

## 3. Compatibility terminal baseline: 80x24

For VT100/ANSI and VT52 compatibility, use **80 columns x 24 rows** as the baseline logical terminal size.

At the normal 7xtgv compatibility raster of 6x10 pixels per character cell:

```text
80 x 6  = 480 physical pixels
24 x 10 = 240 physical pixels
```

Therefore a complete 80x24 compatibility terminal is exactly **480x240 pixels**.

This is particularly convenient on the current baseline 3.5-inch 480x320 TFT:

```text
physical display:       480 x 320
terminal client:        480 x 240
remaining system band:  480 x  80
```

80x24 is preferred over 80x25 because it matches the classic VT-style baseline and preserves a clean 480x240 client rectangle with the existing 6x10 raster.

Native 7xtgv applications are **not required** to use 80x24. Smaller native applications are valid and important for physically smaller TFTs.

---

## 4. Fixed system taskbar / status band

### 4.1 Baseline 480x320 behavior

On a 480x320 display, the normal 80x24 terminal client occupies 480x240 pixels and leaves an 80-pixel-high fixed system area.

That spare region should become a 7xtgv **taskbar/system band**, selectable at either:

- the **top** of the display; or
- the **bottom** of the display.

The terminal's scrolling region must remain the full 80x24 client area and must **not scroll through the taskbar**.

The taskbar is therefore outside the terminal client's coordinate space.

Suggested initial visual treatment:

- neutral silver/grey `PAPER` or another quiet system color;
- clear visual separation from terminal content;
- status text/icons may use normal 7xtgv rendering;
- exact colors are theme data, not hard-coded protocol semantics.

The 80-pixel height happens to equal eight 6x10 text rows, but the taskbar should not be defined as an 8-row terminal. It is a separate system/UI region.

### 4.2 PC mirror

The PC mirrored view should reproduce the same layout:

```text
[ optional taskbar/system band ]
[       terminal client        ]
```

or the reversed order when the taskbar is configured at the bottom.

This is useful because the PC mirror should represent the actual 7xtgv display composition, not merely the terminal's text buffer.

### 4.3 Other physical resolutions

The taskbar height is **display/form-factor policy**, not globally fixed to 80 pixels.

For the present implementation:

- **480x320**: enable the 80-pixel system band by default;
- **320x240**: no dedicated taskbar by default, because the 80x24 terminal already needs all 240 vertical pixels and is wider than the display;
- **smaller displays such as 320x172**: compatibility CP/M is inherently clipped/panned; native 7xtgv apps should be allowed to use smaller logical surfaces;
- **larger displays such as 1280x800**: taskbar/window layout will eventually be a desktop/compositor decision and must not simply consume all pixels left over after one 480x240 terminal.

---

## 5. One logical surface, several physical render scales

The logical terminal/window does not change dimensions when zooming.

Only the physical representation of one logical cell changes:

| View | Physical size per logical cell | Purpose |
|---|---:|---|
| full | 6x10 | real readable text/glyph rendering |
| /2 | 3x5 | bird's-eye summary |
| /3 approx. | 2x3 | bird's-eye summary |
| /6 approx. | 1x2 | bird's-eye summary |
| /10/map | 1x1 | extreme desktop/content map |

The labels `/2`, `/3`, `/6`, `/10` are descriptive, not mathematically exact in both axes.

### 5.1 Critical rule: only 6x10 renders fonts

> **Only the 6x10 view performs actual character-font/glyph lookup.**

The 3x5, 2x3, 1x2, and 1x1 views must **not**:

- downsample the QL/7xtgv font;
- invent 3x5 or 2x3 readable alphabets;
- try to preserve individual letters;
- treat reduced views as miniature terminals intended for reading.

Even 3x5 is a **summary cell**, not a font cell.

The purpose is recognition of:

- window/application geometry;
- occupied text regions;
- `INK` / `PAPER` color structure;
- syntax/status color regions;
- focus/selection;
- important native 7xtgv window-frame boundaries.

---

## 6. Bird's-eye cell summarization

### 6.1 Generic compatibility cell: VT100/ANSI and VT52

Compatibility terminals do not have native 7xtgv `TEXT/WIND/INST/SEMI` semantic cell modes. Treat their cells as generic `TEXT` cells.

For a reduced view:

```text
if logical cell is visually blank/space:
    summary color = PAPER/background
else:
    summary color = INK/foreground
```

The actual character code is normally irrelevant in bird's-eye mode.

Thus `A`, `z`, `#`, punctuation, etc. all become simply **occupied colored cells**.

### 6.2 Native 7xtgv modes

The reduced renderer may use the native cell mode to choose a representative visualization:

#### `TEXT`

- blank/space -> `PAPER`;
- nonblank -> `INK` / effective text attribute color.

#### `WIND`

- **outer window frame/border semantics are special and important**;
- preserve a tiny recognizable border indication at every bird's-eye scale;
- ordinary internal text/content can fall back to the generic occupied/unoccupied color rule;
- it is not necessary initially to preserve every internal TUI box-drawing junction or nested separator.

The first goal is that an application/window rectangle remains recognizable as a **window** when viewed from far away.

#### `INST`

- show representative current attribute/INK color;
- blank/background portions may use PAPER;
- do not attempt to reproduce detailed instrument glyphs at reduced scale.

#### `SEMI`

- treat as a special semantic/color cell;
- preserve representative color/occupancy, not individual semigraphics pixels;
- detailed SEMI behavior remains governed by the full native 7xtgv specification.

### 6.3 WIND outer-frame micro-shapes

For WIND frame cells only, a small shape LUT is useful. It should cover the minimal outer-frame semantics such as:

```text
horizontal outer edge
vertical outer edge
top-left outer corner
top-right outer corner
bottom-left outer corner
bottom-right outer corner
```

At 3x5, 2x3, and 1x2 these can be drawn as tiny edge strokes inside the reduced cell rectangle.

At 1x1, geometry cannot exist inside one cell; the frame is represented by its frame color, and rectangle recognition comes from the sequence of adjacent frame cells.

Do **not** build a complete miniature box-drawing font unless later visual tests prove it useful.

---

## 7. Recommended render interface

Do not create separate terminal implementations for every zoom level.

Keep one logical surface and select a render policy/function:

```cpp
enum ViewScale {
    VIEW_6x10,
    VIEW_3x5,
    VIEW_2x3,
    VIEW_1x2,
    VIEW_1x1,
};
```

Possible high-level entry point:

```cpp
renderCell(logicalX, logicalY, cell, viewScale);
```

Internally, hot paths may use a preselected function pointer/table so that the inner loop does not switch on the scale for every cell:

```cpp
renderCellFn = renderCell6x10;
renderCellFn = renderCellSummary3x5;
renderCellFn = renderCellSummary2x3;
renderCellFn = renderCellSummary1x2;
renderCellFn = renderCellSummary1x1;
```

Naming should deliberately avoid `font3x5`, `font2x3`, etc. These are not fonts.

---

## 8. Viewport, clipping, and panning

Physical placement is derived from logical coordinates and the current summary-cell dimensions:

```text
physicalX = clientOriginX + (logicalX - viewOriginX) * cellWidth
physicalY = clientOriginY + (logicalY - viewOriginY) * cellHeight
```

Then clip against the terminal **client rectangle**, not merely against the raw TFT bounds.

Anything outside the visible client rectangle is ignored safely.

No logical reflow occurs.

### 8.1 320x240 example

At full 6x10:

```text
80x24 terminal = 480x240 pixels
physical TFT    = 320x240 pixels
```

Therefore all 24 rows fit vertically, while only about 53 columns fit horizontally. Horizontal viewport panning is required for the right side of applications, although normal CP/M CCP use often remains workable without frequent panning.

There is no dedicated taskbar by default on this display.

### 8.2 480x320 example

At full 6x10:

```text
80x24 terminal = 480x240
system band    = 480x80
```

No panning is required for a normal 80x24 compatibility terminal.

At 3x5, the same logical 80x24 surface occupies only:

```text
240x120 pixels
```

This is the fundamental future 2x2-overview scale: four maximum-size 80x24 terminal/window surfaces could occupy 480x240 pixels before any desktop margins/chrome are added.

This observation is architectural motivation only; **multiple terminal instances are not part of the current implementation**.

---

## 9. Future desktop/multiwindow model — compatibility constraints only

Do not implement this now, but preserve these assumptions:

- one future terminal/window client may be up to 80x24 compatibility cells on the baseline 480x320 design;
- a future desktop may position several such surfaces in virtual coordinates;
- exactly one will normally have keyboard focus / be the active terminal;
- zoom-out is primarily for navigation and recognition, not reading;
- selecting/focusing one future window can zoom back to its full 6x10 client view;
- native 7xtgv/vmex applications may use smaller client areas;
- larger displays may show several **fully readable** 6x10 windows simultaneously without bird's-eye reduction;
- a 1280x800 panel is therefore a different layout class from the baseline 480x320 TFT.

The current renderer should only assume that a logical surface has an origin and a clipping rectangle, which will make later composition easier.

---

## 10. Terminal personalities versus rendering modes

Keep these concepts separate.

### 10.1 Terminal personality

Current/future parser personalities:

```text
VT100 / ANSI
VT52
native 7xtgv
```

The native 7xtgv mode is a different class of terminal and follows its full cell-mode specification.

### 10.2 Rendering/view mode

Rendering is independent:

```text
normal terminal rendering
bird's-eye summary rendering
experimental xterm-256 plot interpretation   [ANSI only]
```

### 10.3 Do not create a fourth xterm parser

The xterm-256 Mandelbrot feature must **not** become a parallel terminal emulator.

It stays inside the existing VT100/ANSI parser because the Mandelbrot streams use ordinary CSI/SGR syntax.

Internally the relationship should be approximately:

```cpp
terminalPersonality = ANSI_VT100;

ansiRenderInterpretation = ANSI_RENDER_TEXT;
// or
ansiRenderInterpretation = ANSI_RENDER_XTERM256_PLOT;
```

The input decoder, CSI state machine, cursor/control parser, and CP/M byte stream remain the same.

This is the recommended architecture.

---

## 11. Minimal xterm 256-color support inside VT100/ANSI

The attached Mandelbrot sources do **not** use a true bitmap graphics protocol. They emit an xterm-style indexed foreground SGR before each printable sample character:

```text
ESC [ 38 ; 5 ; n m   <printable sample byte>
```

They finish with:

```text
ESC [ 0 m
```

Both sources also define the background indexed-color prefix:

```text
ESC [ 48 ; 5 ; n m
```

Required additions to the existing ANSI SGR parser:

```text
CSI 0 m             reset rendition/colors
CSI 38 ; 5 ; n m    set indexed foreground color, n=0..255
CSI 48 ; 5 ; n m    set indexed background color, n=0..255
```

Recommended trivial additions:

```text
CSI 39 m            restore default foreground
CSI 49 m            restore default background
```

Optional direct RGB (`38;2` / `48;2`) is not required by these demos.

---

## 12. xterm 256-color palette to RGB565

Resolve xterm indexed color to the display's native RGB565 before rendering.

A complete lookup table is only:

```text
256 entries x 16 bits = 512 bytes
```

Palette structure:

- 0..15: conventional terminal colors;
- 16..231: 6x6x6 RGB cube;
- 232..255: grayscale ramp.

For indices 16..231:

```text
k = n - 16
r6 = k / 36
g6 = (k / 6) % 6
b6 = k % 6
level = {0, 95, 135, 175, 215, 255}
```

For indices 232..255:

```text
v = 8 + 10 * (n - 232)
r = g = b = v
```

RGB888 -> RGB565:

```text
rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
```

Useful test colors used by the Mandelbrot table include:

```text
201 -> magenta -> 0xF81F
196 -> red     -> 0xF800
226 -> yellow  -> 0xFFE0
46  -> green   -> 0x07E0
51  -> cyan    -> 0x07FF
21  -> blue    -> 0x001F
```

---

## 13. Two ANSI render interpretations, one parser

### 13.1 Normal ANSI text interpretation

Normal VT100/ANSI behavior remains:

```text
CSI 38;5;n m
```

changes the effective foreground color, after which printable characters are ordinary terminal characters.

At full 6x10 scale they are rendered with the normal compatibility font.

At 3x5, 2x3, 1x2, or 1x1 they use the generic bird's-eye occupied/blank summary rule.

### 13.2 Experimental xterm-256 plot interpretation

When explicitly enabled while still in VT100/ANSI mode:

1. `CSI 38;5;n m` updates `plotColor565` and arms one sample;
2. the **next printable byte** is consumed as one logical color sample instead of as a text glyph;
3. the printable byte's identity is ignored for drawing, so Bill Shen's byte `219` and Phillip Stevens' `#` produce the same result;
4. CR/LF manage sample rows;
5. `CSI 0m` resets attributes and ends/cancels the current armed sample state;
6. ordinary non-plot text before/after the stream can still pass through normal ANSI text handling.

Suggested state:

```cpp
bool plotArmed;
uint16_t plotColor565;
uint16_t plotX;
uint16_t plotY;
```

Suggested sample rule:

```text
on CSI 38;5;n m:
    plotColor565 = palette[n]
    plotArmed = true

on printable byte while plotArmed:
    draw one sample rectangle at plotX, plotY
    plotX++
    plotArmed = false

on CR:
    no vertical advance

on LF:
    plotX = 0
    plotY++

on CSI 0m:
    plotArmed = false
    restore normal ANSI attributes
```

Exact CR handling may be aligned with the current parser, but the plot raster must obtain one fresh X origin per output line.

---

## 14. xterm-256 plot mode uses the same render-scale ladder

Unlike the earlier idea of a separate fixed 1x1/2x2 graphics scale, the current preferred experiment is:

> **Use the existing 7xtgv view-scale ladder for the xterm-256 plot samples as well.**

In plot mode, one Mandelbrot logical sample becomes a **solid rectangle** whose dimensions are the current view scale:

| Current view scale | One plot sample becomes |
|---|---:|
| 6x10 | 6x10 solid color block |
| 3x5 | 3x5 solid color block |
| 2x3 | 2x3 solid color block |
| 1x2 | 1x2 solid color block |
| 1x1 | 1x1 RGB565 pixel |

This is intentionally different from normal ANSI text rendering.

At 6x10, only the top-left portion of the Mandelbrot raster will be visible. That is acceptable and useful for testing clipping and zoom transitions.

Zooming out progressively reveals more of the unchanged logical Mandelbrot sample grid until 1x1 can show the complete image on suitable displays.

The same viewport/panning/clipping logic applies.

The plot is restricted to the **active terminal client rectangle** and must not overwrite the fixed taskbar/system band.

---

## 15. Mandelbrot source behavior relevant to the renderer

The two attached source variants use essentially the same display protocol:

### `brot-bs.z` — Bill Shen / zmac style

- CP/M program origin `0100h`;
- BDOS console calls;
- sample character `219` (`0xDB`), intended as a solid-block character in CP437-like environments;
- `ESC[38;5;` indexed foreground color prefix;
- `ESC[0m` before the completion message.

### `brot-ps.asm` — Phillip Stevens / z88dk-oriented

- sample character `#`;
- the same `ESC[38;5;` indexed foreground protocol;
- supports optimized multiply paths, including Z180-specific code;
- `ESC[0m` before the completion message.

No source-specific path should exist in 7xtgv.

The renderer should consume the byte after an armed indexed-color SGR as the sample, regardless of whether it is `#`, `0xDB`, or another printable byte.

---

## 16. Actual Mandelbrot logical dimensions

Although historical comments describe an output around 240x150, the assembler expressions use integer fixed-point steps:

```text
X: -2*256 .. +1*256
step = 256/80 = integer 3
=> approximately 257 inclusive samples

Y: -5*256/4 .. +5*256/4
step = 256/60 = integer 4
=> approximately 161 inclusive samples
```

So treat the unchanged stream as roughly **257x161 logical samples**.

On the baseline 480x240 terminal client rectangle:

- `6x10`: very strongly clipped, useful as a close-up/test;
- `3x5`: strongly clipped;
- `2x3`: clipped;
- `1x2`: width fits, height is about 322 px and therefore clips vertically;
- `1x1`: the full 257x161 image fits comfortably inside 480x240.

On a 320x240 TFT:

- `1x1` also fits the complete raster;
- larger scales naturally clip and can be panned if useful.

No guest program modification is required.

---

## 17. Keyboard controls: current helper and preferred native mappings

The current PC `terminal.py` helper uses the `Ctrl+]` prefix. Keep it as a development/compatibility control path while native firmware shortcuts become primary.

### 17.1 Existing/preferred control mapping

```text
Ctrl+] q    -> TERM quit helper; CP/M keeps running
Ctrl+] #    -> toggle ## RAW input bypass
Ctrl+] x/c  -> profile Exit / Clear command
Ctrl+] 1/2  -> local ANSI / VT52 mode
Ctrl+] a/e  -> viewport beginning / end
Ctrl+] h/l  -> pan left / right
Ctrl+] r    -> refresh TFT and PC mirror
Ctrl+] p/n  -> previous / next history line
Ctrl+] [/]  -> previous / next history bank
Ctrl+] Ctrl+] -> send literal Ctrl+] to CP/M
```

Preferred firmware-native equivalents currently discussed:

```text
ALT+CTRL+ESC   TERM quit / restart
ALT+CTRL+SPACE refresh TFT and PC mirror

ALT+BACKSPACE  CLS
ALT+ESC        app quit; possibly reboot semantics at CCP/system level

ALT+#          profile / RAW toggle
ALT+PgUp       previous profile
ALT+PgDn       next profile

ALT+HOME       viewport beginning / left extreme
ALT+END        viewport end / right extreme
ALT+Left       viewport pan left
ALT+Right      viewport pan right

Up             previous history line
Down           next history line
```

History may retain Alt-based alternatives where useful.

### 17.2 Profiles 0..15

Future profile extension:

```text
0..15  <=>  hexadecimal 0..F
```

This aligns naturally with CP/M user areas of the current drive.

Direct native profile selection may therefore eventually use:

```text
ALT+0 .. ALT+9
ALT+A .. ALT+F
```

Exact bindings remain a UI policy and are not terminal protocol semantics.

### 17.3 Terminal personality / experimental plot selection

For development, a simple local selector may expose:

```text
1 -> VT100/ANSI normal
2 -> VT52
3 -> native 7xtgv
4 -> VT100/ANSI + xterm-256 plot interpretation
```

The important internal fact is that selections 1 and 4 use the **same ANSI parser**.

Option 4 is only a convenient test/control state, not a fourth terminal standard.

### 17.4 Zoom controls

Proposed:

```text
Ctrl+-   zoom farther out through the scale ladder
Ctrl++   zoom farther in
Ctrl+0   return directly to normal 6x10 full view   [optional but useful]
```

The same scale controls apply to:

- normal ANSI/VT52 bird's-eye summaries;
- native 7xtgv bird's-eye summaries;
- xterm-256 plot sample magnification.

They must not alter logical terminal dimensions or parser state.

---

## 18. Arduino_GFX / display abstraction boundary

All terminal logic should remain independent of the exact TFT controller.

The renderer should operate in RGB565 and use the existing Arduino_GFX/display abstraction for physical output.

Useful low-level operations:

```cpp
drawPixel565(x, y, color);
fillRect565(x, y, w, h, color);
drawSpan565(x, y, pixels, count);   // optional fast path
```

The display layer owns:

- ST/ILI/RGB-panel controller details;
- rotation;
- physical width/height;
- transport details such as SPI/parallel/RGB bus;
- controller-specific acceleration.

The terminal/compositor owns:

- terminal client rectangle;
- taskbar rectangle;
- viewport origin;
- render scale;
- clipping;
- terminal/plot semantics.

---

## 19. Redraw and caching considerations

Bird's-eye view is intentionally simple and should be cheap to redraw.

Recommended behavior:

- keep the logical terminal/cell state exactly as in full view;
- on scale change, redraw the visible client rectangle from that state;
- do not maintain separate miniature terminal buffers unless profiling proves necessary;
- taskbar redraw is independent from terminal scrolling/redraw;
- full refresh (`ALT+CTRL+SPACE` / helper `Ctrl+] r`) redraws both taskbar and terminal client/mirror.

For xterm-256 plot mode, a small logical plot buffer is optional. For the first prototype, direct plotting plus redraw restrictions may be enough. If scale changes must immediately re-render an already-computed Mandelbrot image, retain the logical sample colors in an indexed or RGB565 raster.

A complete 257x161 8-bit indexed sample buffer costs only about 41 KB; an RGB565 buffer about 83 KB. Whether to keep it should depend on target MCU RAM and desired interactive re-zoom behavior.

For small MCUs, plot mode may simply clear/restart on scale change.

---

## 20. Focus and input in the current single-terminal implementation

For now there is exactly one active/focused terminal surface.

Therefore:

- all CP/M keyboard input goes to that terminal;
- ANSI/VT52/native personality switching affects that surface;
- bird's-eye scaling affects that surface;
- xterm-256 plot interpretation, when enabled, affects that active ANSI surface;
- taskbar is system UI and does not become an independent terminal focus target in this phase.

This intentionally avoids premature multiwindow input routing.

---

## 21. Tests to perform on real TFT hardware

The visual result matters more than theoretical elegance. Keep **all four reduced scales initially** and decide later which deserve permanent user exposure.

### 21.1 Bird's-eye visual tests

Use representative 7xtgv screens containing:

- plain TEXT regions;
- colored syntax/text;
- WIND outer window frames;
- nested/internal TUI boxes;
- INST areas;
- SEMI areas;
- active/focused highlights;
- large blank PAPER regions.

Test at:

```text
6x10
3x5
2x3
1x2
1x1
```

Judge primarily:

- can application/window boundaries be recognized quickly?
- is focus obvious?
- is text density visible without pretending text is readable?
- which scale gives the best bird's-eye impression on 3.5/4-inch IPS?
- are 2x3 or 1x2 visually more useful?
- is 1x1 useful as a true desktop map?

Do not remove any scale before physical TFT testing.

### 21.2 Compatibility tests

On 480x320:

- confirm exact 80x24 at 6x10;
- confirm taskbar remains fixed while terminal scrolls;
- verify top/bottom taskbar placement;
- verify ANSI and VT52 panning/zoom do not alter terminal state.

On 320x240:

- confirm full 24-row height;
- confirm horizontal panning;
- confirm no taskbar is required;
- test zoom-out for quick whole-terminal overview.

On 320x172 or similar:

- verify clipping is safe;
- confirm native 7xtgv smaller apps remain practical;
- treat full CP/M compatibility as a constrained use case rather than forcing desktop assumptions onto the display.

### 21.3 Mandelbrot tests

Run both unchanged guest programs:

- `brot-bs` using the `0xDB` sample byte;
- `brot-ps` using `#`.

For each:

1. ANSI normal text at 6x10;
2. ANSI xterm-256 plot at 6x10;
3. plot at 3x5;
4. plot at 2x3;
5. plot at 1x2;
6. plot at 1x1;
7. verify clipping at every scale;
8. verify taskbar is untouched;
9. verify returning from plot interpretation to normal ANSI text does not reset the whole terminal implementation incorrectly.

---

## 22. Benchmark integrity

For historical Mandelbrot comparisons, consume **every byte** emitted by the CP/M guest.

Do not modify the guest to suppress repeated color sequences merely to make it faster.

Host-side optimizations are allowed, for example:

- efficient CSI parsing;
- palette lookup;
- span/rectangle batching;
- deferred physical TFT writes;
- buffering one plot row.

Useful benchmark classes:

1. **integrated system**: RunCPM/Z80 emulation + BDOS console path + terminal parser + renderer + real TFT;
2. **render-neutral emulator**: same guest byte stream and parser, but TFT writes buffered/deferred, to estimate emulator/CP/M cost;
3. optional **paced serial model** if comparison against a physical historical UART benchmark is desired.

Do not impose 115200-baud pacing on normal local RunCPM output unless explicitly testing serial-equivalent behavior.

---

## 23. Non-goals for this phase

Do **not** expand this work into:

- multiple simultaneous RunCPM terminal instances;
- full desktop/window manager;
- vmex task scheduling/window ownership;
- complete xterm emulation;
- SIXEL/ReGIS/Tektronix graphics;
- arbitrary vector/bitmap terminal protocols;
- readable miniature fonts below 6x10;
- automatic terminal reflow on zoom;
- dynamic CP/M row/column negotiation for every TFT size;
- complex miniature reproduction of all WIND/TUI internal junctions.

Those may be future work but are unnecessary to validate the present design.

---

## 24. Recommended implementation order

1. **Formalize the physical layout**: terminal client rectangle + optional taskbar rectangle.
2. Fix the compatibility logical baseline at **80x24**.
3. Confirm normal full rendering at **6x10** remains unchanged.
4. Add the `ViewScale` ladder: 6x10, 3x5, 2x3, 1x2, 1x1.
5. Add the generic bird's-eye occupied/blank summary renderer.
6. Add minimal native 7xtgv mode-aware summaries and WIND outer-frame micro-shapes.
7. Connect existing viewport panning/clipping to all scales.
8. Add top/bottom fixed taskbar rendering and PC-mirror composition.
9. Extend ANSI SGR with `38;5;n` and `48;5;n` xterm indexed color.
10. Add one ANSI-local `ANSI_RENDER_XTERM256_PLOT` flag/state — **no new parser**.
11. Make plot samples use the same 6x10/3x5/2x3/1x2/1x1 scale ladder as solid color rectangles.
12. Test unchanged `brot-bs` and `brot-ps` on real TFTs.
13. Only after visual testing decide whether any intermediate bird's-eye scale should be removed from the user-facing UI.

---

## 25. Architectural summary

The intended layering is:

```text
CP/M / native app output
        |
        v
terminal personality parser
  VT100/ANSI | VT52 | native 7xtgv
        |
        +-----------------------------+
        |                             |
        v                             v
logical terminal cells/state     ANSI xterm-256 plot interpretation
        |                             |
        v                             v
6x10 real glyph renderer         solid sample rectangles
or bird's-eye cell summary       using current ViewScale
        |                             |
        +-------------+---------------+
                      |
                      v
              viewport transform
                      |
                      v
        terminal-client clipping rectangle
                      |
                      v
            Arduino_GFX / RGB565 TFT

separately:
7xtgv taskbar/system band -> fixed system rectangle -> Arduino_GFX / PC mirror
```

The central invariants are:

> **Only 6x10 is text. Reduced views are semantic/color summaries.**

> **xterm-256 plot is an alternate ANSI rendering interpretation, not a separate terminal emulator.**

> **Zoom changes presentation only; it never changes logical terminal state.**

> **The taskbar is outside the scrolling terminal client.**

> **Clipping and panning are normal, especially on physically smaller displays.**

These rules keep the present implementation small while preserving a direct path toward the later 7xtgv/vmex multiwindow desktop.

---

## Appendix A — source basis for the Mandelbrot compatibility path

This specification was checked against the supplied sources:

- `brot-bs.z` — Bill Shen / zmac-style CP/M version, sample byte 219, `ESC[38;5;...m` color output;
- `brot-ps.asm` — Phillip Stevens / z88dk-oriented version, `#` sample byte, same indexed-color SGR output, optimized multiply variants;
- supplied K80 discussion/PDF documenting the benchmark history and the intent to retain the output stream for comparable timing.

The 7xtgv implementation should therefore support the common terminal byte protocol rather than contain Bill/Phillip-specific rendering paths.
