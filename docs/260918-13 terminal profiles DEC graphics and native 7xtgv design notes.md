# 260918-13 — Terminal profiles, DEC graphics, and native 7xtgv design notes

These notes collect conclusions from the 2026-09-18 design discussion around the current
RunCPM/terminal profiles, historical terminal behavior, DEC Special Graphics, and how those
ideas should relate to the already-defined native 7xtgv architecture.

This document is **not a replacement for the native v0.1 specification**. The authoritative
native architecture remains:

- `docs/260910-13 7xtgv v0.1 native specification and implementation plan.md`
- the associated SEMI-mode notes and implementation plan.

The purpose here is to record useful conclusions and next steps without accidentally folding
VT100-specific behavior into the native 7xtgv cell format.

---

## 1. Terminal profile model: three separate layers

The current application profiles have evolved into a useful separation of concerns.

Conceptually there are three independent layers:

```text
application/editor semantics
        |
        +-- WordStar-like controls
        +-- RunCPM CCP controls
        +-- BBC BASIC controls
        +-- MBASIC local-compose behavior
        +-- Turbo Pascal custom command table
        ...

terminal keyboard protocol
        |
        +-- ANSI / VT100-like
        +-- VT52
        +-- ADM-3A / Kaypro
        +-- vendor-specific variants
        ...

terminal output protocol
        |
        +-- ANSI / VT100-ish
        +-- VT52
        +-- ADM-3A
        +-- TeleVideo / Zenith / etc.
```

The useful 7xtgv mapping is therefore:

```text
mode=
    output dialect decoded/rendered by the terminal

input=
    keyboard dialect emitted toward the application

input_adapter=
    optional local editing/translation layer before bytes reach the application

per-key bindings
    explicit application/profile-specific exceptions
```

This separation is intentional and should be preserved.

A screen protocol does **not** automatically define the application's editor controls, and an
application's editor controls do **not** necessarily imply a particular screen protocol.

---

## 2. Current application-profile interpretations

### RunCPM CCP / GENERAL

The RunCPM CCP navigation keys are application/editor controls, not VT100 input simply because
the display decoder is ANSI.

Examples from the current profile include:

```text
physical Left    -> 01 = ^A
physical Right   -> 06 = ^F
physical Home    -> 02 = ^B
physical End     -> 17 = ^W
physical Delete  -> 07 = ^G
physical Insert  -> 16 = ^V
```

The normal Up/Down keys are currently consumed by local command history.

The extra Alt+Up / Alt+Down mappings to `ESC [ A` / `ESC [ B` are ANSI-style escape paths,
but they do not make the CCP itself an ANSI keyboard client.

### MBASIC

The MBASIC profile must not be read as a literal catalogue of what MBASIC itself accepts.

Its `input_adapter=local_compose` means much of the editing behavior belongs to 7xtgv's local
compose editor. The user edits locally and the adapter reconstructs/transports the resulting
line toward MBASIC.

So entries such as Left/Right/Home/Delete in that profile are partly implementation primitives
for the local editor and should not be generalized into a supposed "MBASIC terminal standard".

### BBC BASIC

BBC BASIC has genuine application-specific control-byte handling in the Z80 port, while the
profile also adds 7xtgv-side history and line-management helpers.

It therefore sits between the MBASIC local-compose case and a pure direct-key application.

### TE / WordStar-like controls

TE exposes a very clear WordStar-style control scheme:

```text
^S  left
^D  right
^E  up
^X  down
^C  page up
^R  page down
^Q... extended movement
```

This is application/editor semantics, not a screen terminal protocol.

### Turbo Pascal 3.01

TP301 is an especially useful test application because Turbo Pascal exposes screen installation
and command installation separately.

This makes it a good laboratory for independently exercising:

- output protocol selection;
- keyboard sequence generation;
- application/editor command mapping.

---

## 3. Historical precedent: profiles are not a strange idea

The exact 7xtgv combination is unusual, but its ingredients have historical precedents.

Older software terminal emulators commonly allowed some mixture of:

- switchable terminal emulation;
- terminal-specific keyboard maps;
- user key maps;
- session/site profiles;
- local line editing;
- programmable function keys;
- command windows or command history.

The important lesson is architectural rather than historical:

**terminal personality and keyboard/application personality are not necessarily the same thing.**

7xtgv should keep them orthogonal.

Our current bank/application profile goes somewhat further by making the active **application**
the natural unit of configuration:

```text
application profile
    |
    +-- output decoder
    +-- keyboard personality
    +-- local editing/history behavior
    +-- physical-key overrides
    +-- geometry/zoom
    +-- colors/focus presentation
    +-- clear/exit/on-select helpers
```

This matches the way the system is actually used on RunCPM.

---

## 4. Native 7xtgv remains separate from the terminal front-end

The native 7xtgv v0.1 architecture is already substantially fixed and should not be redesigned
around ANSI/VT100 details.

The four-byte native cell remains:

```text
CHAR_A
CHAR_B
ATTR
EXTA
```

with:

```text
ATTR[7:6] = MM

00 TEXT
01 WIND
10 INST
11 SEMI
```

The current native specification explicitly keeps ANSI features such as underline, reverse,
blink, DEC graphics state, etc. **out of the native cell-format definition**.

That separation should remain.

The correct relationship is:

```text
VT100 / ANSI / VT52 / legacy parser
              |
              v
       resolved display intent
              |
              v
        native 7xtgv client
```

The terminal is one client/front-end of native 7xtgv, not the architecture itself.

---

## 5. SEMI mode stays 2x5 per 6x10 cell

The current SEMI geometry is settled as:

```text
physical cell:      6 x 10

logical SEMI:       2 x 5

each semipixel:     3 x 2 physical pixels
```

Each plane is a 10-bit mask:

```text
bit 0   bit 1
bit 2   bit 3
bit 4   bit 5
bit 6   bit 7
bit 8   bit 9
```

The SEMI interpretation remains either:

- `SEMI_OVERLAY`: A over B over hard desktop;
- `SEMI_INDEXED4`: A+B interpreted as a local two-bit color index.

Do not conflate SEMI with terminal escape-sequence features.

---

## 6. 64 KiB native cell-RAM ceiling: 204x80 is attractive

At four bytes per cell:

```text
204 x 80 x 4 = 65,280 bytes
64 KiB       = 65,536 bytes
remainder    =    256 bytes
```

This gives a very clean architectural upper bound if we choose to make 64 KiB the baseline
native cell-RAM address space.

For a 6x10 unscaled cell on a 1280x800 panel:

```text
204 x 6  = 1224 pixels
80  x 10 =  800 pixels

1280 - 1224 = 56 horizontal pixels spare
```

This is useful rather than wasteful: the spare width can become a gutter/status/sidebar area.

The same geometry scales cleanly for focused zoom:

```text
1x desktop:        204 x 80  -> 1224 x 800
2x focused view:   102 x 40  -> 1224 x 800
4x focused view:    50 x 20  -> 1200 x 800
```

This fits the intended 7xtgv philosophy well:

- whole-desktop overview at native scale;
- recognizable "birdseye" windows;
- zoomed focused windows for actual work;
- 80x24 remains a practical common terminal/application size inside larger viewports.

The 204x80 limit is therefore attractive for both **memory/addressing** and **display geometry**.

It is not yet declared normative here; it is a strong candidate architectural ceiling.

---

## 7. DEC Special Graphics is the next terminal feature

DEC Special Graphics should be added next on the **terminal/front-end side**.

The important concept is not merely "32 extra line-drawing glyphs". VT100 uses character-set
designation and invocation.

There are two character-set slots:

```text
G0
G1
```

A slot can be designated as ASCII or DEC Special Graphics.

### Designation

```text
ESC ( B       designate ASCII into G0
ESC ( 0       designate DEC Special Graphics into G0

ESC ) B       designate ASCII into G1
ESC ) 0       designate DEC Special Graphics into G1
```

The syntax can be thought of as:

```text
ESC ( X       G0 := X
ESC ) X       G1 := X
```

### Invocation

```text
0F   SI   Shift In       invoke G0
0E   SO   Shift Out      invoke G1
```

A common arrangement is:

```text
G0 = ASCII
G1 = DEC Special Graphics
```

Normal text uses G0. The application sends SO to draw line graphics through G1, then SI to return
to ASCII.

---

## 8. Why DEC graphics looks strange in byte traces

DEC Special Graphics reuses ordinary printable ASCII positions while that character set is
active.

Typical important mappings include:

```text
j   lower-right corner
k   upper-right corner
l   upper-left corner
m   lower-left corner
n   crossing lines
q   horizontal line
x   vertical line
t   left tee
u   right tee
v   bottom tee
w   top tee
```

Therefore a byte trace such as:

```text
SO l q q q q k SI
```

does not mean that the application intends to show the letters `lqqqqk`. With G1 designated
as DEC graphics, those character positions render as box-drawing glyphs.

This is exactly why proper G0/G1 state matters.

---

## 9. Recommended DEC-graphics implementation boundary

The parser should resolve the active character set **when the character is written**.

Conceptually:

```text
incoming printable byte
        +
active G0/G1 selector
        +
G0/G1 designation
        |
        v
resolved glyph identity
        |
        v
screen/cell storage
```

For example:

```text
byte 'q'
G1 active
G1 = DEC Special Graphics

        -> DEC horizontal-line glyph
```

Do **not** store `'q'` and depend on the current G1 state later during rendering.

Changing G1 later must not retroactively change characters already displayed.

This keeps terminal-parser state out of native 7xtgv.

The native TEXT glyph resources can simply contain the required DEC line-drawing/symbol glyph
shapes at suitable internal codes. The native cell itself does not need to know what G0 or G1
means.

---

## 10. DEC double-width/double-height lines are a separate problem

VT100 also has line-size controls such as:

```text
ESC # 3     double-height, top half
ESC # 4     double-height, bottom half
ESC # 5     normal
ESC # 6     double-width
```

These are legitimate VT100 features but they do not fit the native tiled 6x10 desktop model as
naturally as ordinary character-set translation.

Current conclusion:

- do **not** redesign the native four-byte cell around DEC line sizing;
- treat it as a VT100 front-end/emulation issue;
- implement later only if compatibility/testing shows it is worthwhile.

Native 7xtgv is intended to keep a fixed cell fabric and achieve presentation scaling through
the desktop/window zoom model, not through per-line VT100 geometry.

---

## 11. Smooth 1-pixel scrolling should also remain outside the cell format

Smooth scrolling is desirable and should be explored soon, but it should not consume native cell
bits.

There are two broad hardware cases.

### TFT controllers with internal framebuffer/GRAM

On smaller modules, controller hardware may offer vertical scroll/start-address facilities.

A full-screen terminal scroll can then potentially be very cheap:

```text
display_start_y += 1
```

rather than physically copying all pixels.

### RGB/MIPI-RGB style larger panels

Larger panels are commonly driven by a continuous framebuffer/DMA raster path.

Here smooth scrolling is more naturally implemented in the framebuffer/compositor path, through
addressing/ring-buffer/line-offset techniques appropriate to the HAL.

For a **window** inside a desktop, panel-wide controller scrolling is not sufficient anyway.

A future window compositor can instead maintain a sub-cell vertical phase:

```text
window logical rows
        |
sub-cell Y phase = 0..9
        |
cell rasterizer
        |
desktop compositor
```

A smooth transition can progress through phases 0..9 and then advance the logical text row.

Again, no changes to `CHAR_A / CHAR_B / ATTR / EXTA` are required.

---

## 12. Native desktop/window philosophy

The emerging native system is closer to a very small hardware-friendly tiled compositor than to
a conventional terminal chip.

The broad model is:

```text
hard desktop/background
        |
        +-- cell windows
        +-- focus selection
        +-- inactive-window dimming
        +-- birdseye overview
        +-- zoomed focused view
        +-- gutters / status / instrumentation
```

This is compatible with the current v0.1 rule that smart-terminal software owns:

- window buffers;
- position and dimensions;
- clipping;
- z-order;
- focus;
- titles;
- application association;
- flattening into visible native cell RAM.

The low-level 7xtgv core should stay simple.

The analogy to a tiled desktop / Hyprland-like workflow is useful at the conceptual UI level,
but implementation remains intentionally much simpler and strongly cell-oriented.

---

## 13. Immediate next step

The next small terminal feature should be:

**proper DEC Special Graphics with G0/G1 designation and SI/SO invocation.**

Suggested minimal implementation scope:

1. Add parser state for G0 and G1 designation.
2. Add SI/SO active-set switching.
3. Add the DEC Special Graphics glyph set needed by VT100 applications.
4. Resolve printable bytes to internal glyph identities at write time.
5. Add focused tests for:
   - `ESC ( B`
   - `ESC ( 0`
   - `ESC ) B`
   - `ESC ) 0`
   - SI / SO switching
   - mixed ASCII + DEC line drawing
   - already-written glyphs remaining unchanged after later designation changes.
6. Keep all of this out of the native 7xtgv cell-format definition.

After that, smooth-scroll experiments are a natural next display-side enhancement, preferably
implemented per-HAL without constraining the native cell architecture.

---

## 14. Design principle to preserve

The most important conclusion from this discussion is the separation:

```text
historical terminal protocol details
        !=
native 7xtgv display architecture
```

Terminal parsers may be complicated because historical terminals are complicated.

Native 7xtgv should remain small, regular, fixed-cell, HDL-friendly and useful beyond terminals.

That separation is what allows the same native compositor to serve:

- CP/M terminals;
- programming/editor TUIs;
- native tiled windows;
- WIND chrome;
- INST panels;
- SEMI graphics;
- full-desktop overview;
- zoomed focused applications.

