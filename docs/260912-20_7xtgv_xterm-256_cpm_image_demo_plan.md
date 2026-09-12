# 7xtgv xterm-256 CP/M Image Demo Plan

**Project:** 7xtgv terminal / RunCPM compatibility layer  
**Purpose:** Stream arbitrary indexed-color images from CP/M through the existing VT100/ANSI xterm-256 path and render them as 1-pixel plot samples on RGB565 TFT displays.  
**Status:** design/implementation plan  
**Date:** 2026-09-12

---

## 1. Goal

Add a very small image-demo toolchain built around the xterm-256 support already implemented in the 7xtgv VT100/ANSI compatibility terminal.

The intended path is:

```text
PNG/JPEG/BMP on PC
      |
      v
png2x256.py
      |
      v
raw xterm-256 indexed image (.X256)
      |
      v
CP/M viewer: X256.COM
      |
      v
ordinary ANSI/xterm-256 output stream
      |
      v
7xtgv VT100/ANSI parser
      |
      v
xterm-256 plot interpretation
      |
      v
RGB565 TFT
```

No image decoder is required on the Z80 side. CP/M only reads an indexed image file and emits ordinary terminal control sequences plus printable bytes.

The guest program remains unaware that the terminal interprets one printable cell as one plotted sample.

---

## 2. Design principles

1. **Do not introduce another terminal protocol.**
   - Use the existing VT100/ANSI parser.
   - Use normal xterm-style `CSI 38;5;n m` indexed foreground color.
   - The special behavior exists only in the terminal renderer/plot interpretation.

2. **Keep the CP/M viewer tiny.**
   - No PNG/JPEG/BMP decoding.
   - No large framebuffer.
   - No requirement that the whole image fit into Z80 RAM.
   - Stream one scanline or a small chunk at a time from disk.

3. **Keep the PC converter simple and deterministic.**
   - Convert source RGB to the exact xterm-256 palette.
   - Optionally resize/crop before conversion.
   - Optional dithering.

4. **Preserve the benchmark/demo value.**
   - The terminal still receives a real ANSI/xterm output stream from CP/M.
   - The viewer can intentionally run in either verbose or optimized output modes.

5. **Allow images larger than the physical TFT.**
   - The terminal renderer clips normally.
   - Future viewport panning can reveal other portions.
   - This deliberately tests clipping and large logical images.

---

## 3. xterm-256 plot behavior required from 7xtgv

The renderer should treat xterm color state as persistent, exactly as normal SGR semantics do.

```text
CSI 38;5;n m
```

sets the current foreground/plot color.

In the special plot interpretation:

```text
printable byte -> emit one logical image sample using current foreground color
```

Therefore this:

```text
ESC[38;5;196m##########
```

produces ten red samples, not only one.

This is preferred over requiring a new SGR sequence before every pixel.

### 3.1 Line handling

Recommended plot cursor behavior:

```text
CR    -> x = 0
LF    -> y = y + 1
```

If the CP/M viewer emits `CR LF` per line, each image row starts at x=0.

### 3.2 Plot exit/reset

```text
CSI 0 m
```

resets SGR state and terminates the active colored plot stream cleanly.

The renderer may then return to ordinary terminal text rendering according to the currently selected terminal view mode.

---

## 4. Initial `.X256` file format

Use an intentionally trivial raw indexed format.

### 4.1 Version 1 header

Suggested little-endian layout:

```text
offset  size  meaning
------  ----  -----------------------------------------
0       4     magic: "X256"
4       1     format version = 1
5       1     flags
6       2     width  (uint16 little-endian)
8       2     height (uint16 little-endian)
10      ...   pixel data, row-major, one byte per pixel
```

Initial flags:

```text
bit 0   reserved for RLE payload
bit 1   reserved for future transparency/mask semantics
bit 2-7 reserved, must be zero
```

For the first implementation:

```text
flags = 0
```

and each pixel is directly one xterm palette index `0..255`.

### 4.2 Raw image size examples

```text
480 x 320  =   153,600 bytes pixel data
640 x 480  =   307,200 bytes
800 x 600  =   480,000 bytes
1024 x 768 =   786,432 bytes
1280 x 800 = 1,024,000 bytes
```

These files do not need to fit in Z80 RAM because the viewer streams them from disk.

---

## 5. Optional `.X256` RLE extension

RLE is useful but should be a second step, not required for the first working demo.

### 5.1 Why RLE may help

Images with large flat regions can become much smaller, and the CP/M viewer can emit long same-color runs efficiently.

It also maps naturally to persistent SGR state:

```text
run color changes -> emit one CSI 38;5;n m
run length        -> emit N printable plot bytes
```

### 5.2 Simple RLE record proposal

When header `flags bit 0 = 1`, payload can contain:

```text
uint16 count
uint8  color_index
```

repeated until `width * height` samples have been reconstructed.

Suggested semantics:

- little-endian `count`
- `count >= 1`
- runs may cross scanline boundaries in the file
- viewer internally tracks x/y and emits CR/LF as required at row boundaries

A 16-bit count keeps the decoder tiny and avoids escape-value tricks.

Alternative later format, if desired:

```text
uint8 count
uint8 color
```

with `count=0` meaning 256, but the 16-bit version is simpler and clearer.

### 5.3 Recommendation

Implement in this order:

```text
v1 raw X256 first
then optional v1+RLE using flags bit 0
```

Do not delay the first viewer for compression.

---

## 6. PC-side converter: `png2x256.py`

Use Python + Pillow.

### 6.1 Required input

Support at minimum:

```text
PNG
JPEG
BMP
```

Anything Pillow can load is acceptable.

### 6.2 Required output

```text
.X256 raw indexed file
```

with the exact 256-entry xterm palette.

### 6.3 xterm palette construction

Indices:

```text
0..15      base/system colors
16..231    6 x 6 x 6 RGB cube
232..255   grayscale ramp
```

Color-cube levels:

```text
0, 95, 135, 175, 215, 255
```

For index `n` in `16..231`:

```text
k = n - 16
r = level[k / 36]
g = level[(k / 6) % 6]
b = level[k % 6]
```

Grayscale indices `232..255`:

```text
8, 18, 28, ... 238
```

The converter should build a fixed 256-color RGB palette and map source pixels to the nearest palette entry.

### 6.4 Quantization options

Required modes:

```text
--dither none
--dither fs
```

where `fs` means Floyd-Steinberg dithering.

Recommended default:

```text
none
```

because it is deterministic and preserves pixel-art edges.

For photographs, `--dither fs` should often look substantially better.

### 6.5 Resize/crop options

Useful options:

```text
--width N
--height N
--fit contain
--fit cover
--crop X,Y,W,H
```

For first implementation, it is enough to support:

```text
--size WIDTHxHEIGHT
```

with a simple resize.

Do not require resizing: larger-than-screen images are a desired test case.

### 6.6 Optional RLE generation

Later:

```text
--rle
```

sets header flag bit 0 and writes `(count, color)` runs instead of raw bytes.

---

## 7. CP/M viewer: `X256.COM`

The viewer should be as small and boring as possible.

### 7.1 Command line

Suggested syntax:

```text
X256 IMAGE.X25
```

or:

```text
X256 IMAGE.X256
```

Use whichever extension is convenient; `.X256` is clearer but longer than classic CP/M naming conventions.

A CP/M-friendly 3-character extension could be:

```text
.X25
```

Recommended initial choice:

```text
filename.X25
```

while the file magic remains `X256`.

### 7.2 Operation

1. Open file.
2. Validate magic/version.
3. Read width/height.
4. Reset local output state.
5. Stream pixels.
6. Emit xterm indexed foreground color only when it changes.
7. Emit one printable plot byte per sample.
8. At row end emit CR/LF.
9. At image end emit `CSI 0 m`.
10. Close file and return to CP/M.

### 7.3 Printable plot byte

Use a normal ASCII character, for example:

```text
#
```

The 7xtgv plot renderer ignores glyph shape and treats each printable byte as one sample, so the exact byte does not matter.

Using `#` avoids dependence on CP437 or extended character sets.

### 7.4 Color-output optimization

Track the previous color:

```c
if (pixel != previous_pixel) {
    emit_sgr_256(pixel);
    previous_pixel = pixel;
}
emit('#');
```

This should be the normal viewer mode.

It preserves normal terminal semantics and can reduce output drastically on flat-color images.

### 7.5 Optional benchmark mode

A diagnostic option may intentionally emit SGR for every pixel:

```text
X256 /V IMAGE.X25
```

where `/V` means verbose/full stream.

Then every sample becomes roughly:

```text
ESC [ 3 8 ; 5 ; n m #
```

This is useful as a deliberate terminal-output stress benchmark and resembles the existing Mandelbrot demos more closely.

Normal viewing should use color-change suppression.

---

## 8. Z80 memory strategy

Do not allocate a full image buffer.

Possible implementations:

### Minimal byte-stream implementation

```text
read small file chunk
process pixels immediately
```

### Scanline implementation

For width <= some configured limit:

```text
read one row
emit one row
```

A row buffer is convenient but not required.

Examples:

```text
480 pixels  -> 480-byte indexed row buffer
1280 pixels -> 1280-byte indexed row buffer
```

Even on CP/M this is manageable, but the simplest stream decoder can avoid any width-dependent allocation.

RLE decoding likewise requires only a run counter and current color.

---

## 9. Output-volume expectations

The raw `.X256` file contains exactly one byte per source pixel.

The ANSI stream can be much larger.

### Worst/verbose case

If every pixel receives its own color escape, each pixel may require around 10-12 terminal bytes.

For 480x320:

```text
153,600 image samples
roughly 1.5-1.8 MB terminal output
```

On a classic real Z80 this can take a very long time and is acceptable as a stress/demo case.

### Optimized color-change case

For runs of equal colors:

```text
one SGR sequence per color run
one printable byte per pixel
```

This can be much faster for posters, pixel art, diagrams, and flat backgrounds.

Photographs will still generate many color changes.

---

## 10. Display behavior

The CP/M viewer should not know the physical TFT resolution.

The terminal renderer handles:

```text
logical plot coordinates
viewport origin
current plot scale
physical clipping
RGB565 conversion
```

### Examples

#### 480x320 image on 480x320 TFT

At 1 physical pixel per logical sample:

```text
exact full-screen fit
```

#### 640x480 image on 480x320 TFT

```text
top-left 480x320 visible
remainder clipped
```

Future viewport panning may reveal the rest.

#### 1280x800 image on 1280x800 TFT

```text
exact fit on the ESP32-P4 10-inch panel
```

This makes the same CP/M viewer useful across all 7xtgv display modules.

---

## 11. Relationship to 7xtgv zoom levels

The xterm-256 image plot remains part of the active VT100/ANSI terminal mode.

It does **not** require another parser.

The plot interpretation may use the same selected physical sample-size ladder as the existing view scaling, if desired:

```text
6x10
3x5
2x3
1x2
1x1
```

For image mode these dimensions mean **sample block size**, not character rendering.

Recommended practical image modes:

```text
1x1   canonical image view
2x2   optional simple enlargement if later added
```

The existing terminal zoom controls may still be used experimentally if current implementation already maps plot samples through them.

No attempt should be made to render text glyphs inside an image sample.

---

## 12. Suggested first demo images

Use several categories because they expose different renderer characteristics.

### A. xterm palette chart

A generated 256-color reference image.

Purpose:

```text
validate palette mapping
validate RGB565 conversion
spot swapped channels or incorrect indices
```

### B. Smooth RGB gradients

Purpose:

```text
inspect cube quantization
compare dithering on/off
look for banding
```

### C. Pixel art / retro screenshot

Purpose:

```text
check sharp edges
check no-dither mode
exercise repeated color runs
```

### D. Photograph

Purpose:

```text
stress 256-color quantization
stress terminal throughput
compare Floyd-Steinberg dithering
```

### E. Oversized image, e.g. 640x480 or 1024x768

Purpose:

```text
verify clipping
later verify panning
verify no dependency on physical display dimensions
```

### F. 1280x800 image

Purpose:

```text
future ESP32-P4 / 10-inch full-resolution demo
```

---

## 13. Validation tests

### Converter tests

- Verify header magic/version.
- Verify width/height.
- Verify exact payload length for raw mode.
- Verify palette index range `0..255`.
- Generate a 256-color test chart with known indices.
- Compare dithering enabled/disabled output.

### Viewer tests

- Invalid magic rejected.
- Unsupported version rejected.
- Truncated file detected.
- 1x1 image works.
- 256x1 palette strip works.
- CR/LF row transitions correct.
- SGR emitted only on color changes in optimized mode.
- `CSI 0m` emitted on clean completion and ideally on error exit.

### Terminal tests

- `CSI 38;5;n m` changes current plot color.
- Repeated printable bytes after one SGR generate repeated same-color samples.
- CR resets x.
- LF advances y.
- Off-screen pixels are safely clipped.
- Plot mode exit returns to normal VT100/ANSI rendering.
- RGB565 mapping matches known palette test colors.

---

## 14. Benchmark modes worth keeping separate

It is useful to distinguish three measurements.

### 1. Image conversion benchmark

PC-side only. Not relevant to Z80 speed.

### 2. CP/M terminal-stream benchmark

Measure elapsed time from `X256.COM` start to completion.

Includes:

```text
CP/M file I/O
Z80/emulated-Z80 execution
integer decimal conversion for SGR indices
terminal output calls
ANSI parser
TFT renderer
```

### 3. Pure renderer benchmark

Host/firmware-side diagnostic feeding prepared indexed samples directly.

This isolates:

```text
palette lookup
clipping
RGB565 write rate
Arduino_GFX/display controller throughput
```

Do not confuse this with the CP/M demo benchmark.

---

## 15. Performance notes for real Z80 versus RunCPM

On a classic Z80, large photographs may be intentionally slow because:

- disk reads are slow,
- decimal ANSI color sequences are verbose,
- BDOS/console output overhead is significant,
- serial transport may dominate on real hardware,
- highly varied photographs cause many color changes.

This is acceptable.

The main target for larger images is the much faster RunCPM environment on MCUs, especially the future ESP32-P4 implementation.

The same file and CP/M viewer should nevertheless remain valid on actual Z80 CP/M systems.

---

## 16. Non-goals

For the first implementation, do **not** add:

- PNG/JPEG decoder to CP/M
- GIF animation
- Unicode half-block tricks
- SIXEL
- ReGIS
- Tektronix graphics
- kitty/iTerm image protocols
- truecolor `38;2;r;g;b`
- guest-visible knowledge of TFT resolution
- guest-side framebuffer
- arbitrary scaling/filtering in the CP/M viewer
- sophisticated compression

The point is specifically to exploit the already-working xterm-256 ANSI path in the simplest possible way.

---

## 17. Implementation order

### Phase 1 - minimal raw demo

1. Confirm persistent-color behavior in 7xtgv plot mode.
2. Implement `.X25/.X256` raw header.
3. Implement `png2x256.py` without dithering.
4. Implement `X256.COM` streaming raw files.
5. Add color-change suppression.
6. Test 256-color chart.
7. Test 480x320 image.
8. Test oversized 640x480 image and clipping.

### Phase 2 - image quality

1. Add Floyd-Steinberg dithering.
2. Add source resize option.
3. Test photograph versus pixel art.
4. Add several prepared demo images.

### Phase 3 - compression/benchmark options

1. Add optional RLE payload.
2. Add `/V` full-SGR-per-pixel benchmark mode.
3. Compare raw versus RLE disk sizes.
4. Compare optimized versus verbose terminal-output time.

### Phase 4 - future viewport integration

1. Use existing/future terminal viewport panning for oversized images.
2. Test on 320x240, 480x320 and 1280x800 targets.
3. Keep all display dimensions outside the CP/M file/viewer format.

---

## 18. Minimal pseudocode

### PC converter

```python
img = load_image(filename)
img = optional_resize(img)

palette = build_xterm256_palette()
indices = quantize(img, palette, dithering=option)

write_header("X256", version=1, flags=0, width=img.width, height=img.height)
write(indices)
```

### CP/M viewer

```c
read_header();
prev = -1;

for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
        p = read_byte();

        if (verbose || p != prev) {
            emit("\x1b[38;5;");
            emit_decimal(p);
            emit('m');
            prev = p;
        }

        emit('#');
    }

    emit('\r');
    emit('\n');
}

emit("\x1b[0m");
```

### RLE viewer extension

```c
while (samples_remaining) {
    count = read_u16();
    color = read_u8();

    set_color_if_needed(color);

    while (count--) {
        emit('#');
        advance_x_y_and_emit_crlf_at_row_end();
    }
}
```

---

## 19. Final recommendation

Start with **raw one-byte-per-pixel `.X25` + color-change suppression**.

That combination is small, transparent, easy to debug, and already sufficient for arbitrary 256-color images much larger than the current 480x320 TFT.

Treat RLE as a later optional storage optimization, not as part of the minimum viable demo.

The important architectural property is retained throughout:

> CP/M emits ordinary ANSI/xterm-256 text output; 7xtgv alone decides that, in the selected plot interpretation, each printable byte represents one RGB565 image sample.

