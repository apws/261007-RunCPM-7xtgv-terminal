# 7xtgv Windows SDL2 desktop emulator plan

Date: 2026-09-14

Status: implementation plan / reference architecture

## 1. Goal

Create a **desktop reference implementation of the real 7xtgv terminal + RunCPM machine**, distributed primarily as a Windows executable and expected to run both:

- natively on Windows 10/11;
- under Wine on Linux.

The desktop program is not intended to become another independent terminal emulator and must not duplicate 7xtgv terminal semantics. Its purpose is to execute the same 7xtgv parser, terminal state, fonts, palettes, compositor and rendering logic used by MCU/TFT builds, while replacing only the physical hardware-facing display/input/storage pieces with desktop implementations.

Primary target:

```text
7xtgv.exe
    |
    +-- RunCPM
    +-- real 7xtgv terminal core
    +-- Arduino_GFX-compatible desktop shim
    +-- RGB565 logical framebuffer
    +-- SDL2 window/input
    +-- host filesystem-backed CP/M drives
```

The desktop build should also become the preferred **reference and automated test target** for terminal development, so Codex and human developers can test 7xtgv without flashing hardware or owning a specific MCU/TFT module.

---

## 2. Architectural principle

Do **not** port the Arduino framework to desktop.

Do **not** make SDL2 a second independent 7xtgv renderer API if this can be avoided.

Instead, emulate only the small subset of the `Arduino_GFX` API that 7xtgv actually uses.

Conceptually:

```text
                         portable 7xtgv
                 parser / state / compositor
                              |
                    Arduino_GFX-shaped API
                              |
              +---------------+---------------+
              |                               |
        MCU implementation              desktop implementation
        real Arduino_GFX                compatibility shim
              |                               |
        ST7789 / AXS / RGB               RGB565 framebuffer
              |                               |
             TFT                             SDL2
                                               |
                                        desktop window
```

This preserves one graphics-facing contract for the terminal code.

The desktop shim should be intentionally narrow: implement only methods actually required by 7xtgv, tests and planned native compositor work. Do not attempt to reproduce the complete Arduino_GFX library or all its controller classes.

---

## 3. Important existing proof points in our repository

The current repository already contains a host-side `Arduino_GFX_Library.h` test stub under:

```text
src/7xtgv-vt100-runcpm/tests/stubs/Arduino_GFX_Library.h
```

It already provides:

- an `Arduino_GFX` class;
- a 320x240 RGB565 framebuffer;
- `begin()`;
- `width()` / `height()`;
- `fillScreen()`;
- `fillRect()`;
- `draw16bitRGBBitmap()`;
- minimal fake bus/controller classes.

This is effectively the first prototype of the desired desktop graphics abstraction. The SDL2 desktop backend should evolve from this idea rather than introducing a completely unrelated rendering layer.

The current 7xtgv terminal/RunCPM code also already separates RunCPM console traffic from terminal runtime via functions such as:

```text
terminalSetup()
terminalLoop()
terminalWaitReady()
virtualAvailable()
virtualRead()
virtualWrite()
```

That separation is valuable and should be retained.

---

## 4. Recommended desktop graphics architecture

### 4.1 Keep an explicit logical RGB565 framebuffer

Prefer this:

```text
7xtgv rendering calls
       |
Arduino_GFX-compatible shim
       |
RGB565 framebuffer
       |
SDL_UpdateTexture / equivalent
       |
SDL renderer/window
```

rather than directly translating every `fillRect()` or `drawPixel()` call into immediate SDL drawing commands.

Reasons:

1. RGB565 is already the natural display representation for our MCU/TFT targets.
2. It preserves the same quantisation behaviour as real panels.
3. It gives deterministic frame contents for tests.
4. It allows PNG/framebuffer dumping without involving SDL.
5. SDL presentation can be batched independently of terminal updates.
6. It avoids tying 7xtgv drawing semantics to SDL renderer behaviour.
7. It makes headless tests possible.

A desktop shim may therefore look approximately like:

```cpp
class Arduino_GFX {
public:
    bool begin();
    int16_t width() const;
    int16_t height() const;

    void fillScreen(uint16_t color);
    void drawPixel(int16_t x, int16_t y, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void draw16bitRGBBitmap(int16_t x, int16_t y,
                            const uint16_t *pixels,
                            int16_t w, int16_t h);

    // Add only methods actually required by current/future 7xtgv code.
};
```

Internally it writes only to a logical RGB565 surface.

### 4.2 SDL2 presentation layer

SDL2 should be responsible for only:

- creating the window;
- creating/updating a texture from the RGB565 framebuffer;
- integer scaling;
- fullscreen/window management;
- keyboard events;
- clipboard later if useful;
- quit/window events;
- timing/event pump where appropriate.

It must not know anything about VT100, VT52, ANSI, xterm-256, CP/M, 7xtgv modes or application profiles.

### 4.3 Native resolution remains authoritative

For a 480x320 target:

```text
logical framebuffer = 480x320 always
```

Window sizes may be:

```text
480x320   1x
960x640   2x
1440x960  3x
```

Use nearest-neighbour/integer scaling by default so the 6x10 QL glyphs remain true pixel art. Fractional scaling may be optional but must not change the logical framebuffer.

This is a machine emulator, not a freely reflowing desktop terminal.

---

## 5. Display profiles / module emulation

The desktop program should eventually support named logical display profiles corresponding to our hardware classes, for example:

```text
--profile rp2350-touch-lcd-2
--profile jc8048w535
--profile jc8012p4a1c
```

or initially simply:

```text
--display 320x240
--display 480x320
--display 1280x800
```

Important distinction:

- emulate **logical 7xtgv-visible geometry and capabilities**;
- do not emulate SPI timing, panel controller registers or irrelevant electrical details unless explicitly needed for a hardware-specific test.

The emulator is a functional display-machine reference, not an electrical simulator.

---

## 6. Arduino compatibility scope

Do not attempt to make Arduino itself run on Windows.

Provide small desktop shims only where shared code actually requires Arduino symbols.

Likely examples:

```text
Arduino.h desktop shim
    uint8_t / byte
    millis()
    micros()
    delay()
    min/max compatibility if required
    perhaps Serial debug output

Arduino_GFX_Library.h desktop shim
    Arduino_GFX
    minimal fake bus/controller constructors if current setup code expects them
```

Hardware-only concepts should be excluded or replaced at compile time:

```text
pinMode()
digitalWrite()
PWM/backlight GPIO
SPI controller setup
SD-card pin setup
USB host stack
MCU reboot APIs
FreeRTOS / RP2350 multicore ownership
```

Do not gradually grow a fake Arduino environment merely to preserve hardware-specific code. If a subsystem is truly platform-specific, put a clean desktop implementation beside the MCU one.

---

## 7. RunCPM integration

Preferred desktop architecture:

```text
RunCPM core
    |
    | direct console calls
    v
7xtgv virtual console transport
    |
    v
7xtgv parser / screen / compositor
    |
    v
Arduino_GFX desktop shim
    |
    v
RGB565 framebuffer
    |
    v
SDL2
```

Avoid PTY or ConPTY for the integrated RunCPM mode.

The point is to test our exact internal machine, not a child process connected through an operating-system terminal stack.

Where practical, base the desktop RunCPM layer on upstream POSIX/Windows filesystem abstractions instead of carrying Arduino SD/SdFat concepts into the desktop build.

The CP/M drive layout should still look familiar to the project. A simple portable directory package is desirable:

```text
7xtgv/
    7xtgv.exe
    A/
      0/
      1/
      ...
    B/
    ...
    7xtgv.ini
```

or use the existing repository `cpmdrive` material during development.

The desktop build should not require disk-image emulation merely for symmetry with vintage systems; RunCPM's host-directory model remains appropriate.

---

## 8. RunVT: what role should it have?

RunVT is highly relevant as **reference code and build-system inspiration**, but it should not sit in the terminal data path of the final integrated 7xtgv emulator.

### 8.1 Do not stack 7xtgv on RunVT terminal semantics

Avoid:

```text
RunCPM -> 7xtgv -> RunVT terminal parser -> SDL
```

and avoid:

```text
RunCPM -> RunVT parser -> 7xtgv-like renderer
```

Both would create two terminal state machines and would make tests ambiguous: a rendering difference could come from RunVT semantics rather than 7xtgv.

### 8.2 What can be reused from RunVT

Study and selectively reuse ideas/code patterns for:

- SDL2 initialization;
- window setup;
- renderer/texture lifecycle;
- keyboard/event pumping;
- Windows MinGW build setup;
- static SDL2 linkage into a single `.exe`;
- clipboard handling if later wanted;
- clean quit behaviour;
- frame-presentation throttling;
- Windows/Wine-friendly packaging.

RunVT's current design is especially useful as evidence that one small Windows executable with statically linked SDL2 is practical.

### 8.3 What should not be reused as architecture

Do not adopt RunVT's:

- VT100/ANSI parser;
- screen state;
- Sixel compositor;
- character renderer;
- terminal reply logic;
- PTY/ConPTY child-process transport for integrated RunCPM mode.

Those are RunVT's terminal machine, while the purpose of this project is to exercise our own.

### 8.4 Optional later standalone terminal mode

A second mode may eventually be useful:

```text
external serial / TCP / pipe / child process
        |
        v
     7xtgv core
        |
       SDL2
```

For this optional mode, RunVT's PTY/ConPTY abstraction can be studied again.

However, keep it separate from the first milestone. The first desktop executable should run integrated RunCPM directly.

---

## 9. Why Windows-first is acceptable

The initial desktop distribution may deliberately be Windows-only:

```text
7xtgv.exe
```

with Linux users running the exact same executable under Wine.

Benefits:

- one canonical desktop binary;
- one build configuration to support first;
- easier release/testing matrix;
- likely very Wine-friendly workload;
- SDL2 already abstracts the window/input layer;
- no ConPTY requirement for integrated RunCPM;
- no COM/browser/framework dependencies;
- portable ZIP distribution is possible;
- Codex can cross-compile the Windows artifact on Linux and run/test it through Wine.

A native Linux SDL build can always be added later if there is a concrete need. The architecture must not deliberately prevent it, but it does not need to be the first deliverable.

---

## 10. Windows build strategy

Recommended first toolchain:

```text
MinGW-w64 + SDL2 development package
```

Target outcome:

```text
7xtgv.exe
```

Prefer static SDL2 linkage if licensing/build details remain acceptable, following the same practical model as current RunVT Windows builds.

Initial build system may be a simple Makefile before considering CMake.

Desirable targets:

```text
make desktop-win
make desktop-win-debug
make desktop-win-headless-tests
```

Optional later:

```text
make desktop-linux
```

Do not require Visual Studio for the canonical build, because Linux-hosted cross-compilation is useful for our Codex workflow.

---

## 11. Input / keyboard architecture

SDL keyboard events should enter the same 7xtgv unified input/profile layer used by hardware.

Do not directly emit CP/M bytes from the SDL event loop except through the same translation path used by the real terminal.

Conceptually:

```text
SDL key event
    |
PC key normalization
    |
7xtgv unified key event
    |
application profile / history / RAW handling
    |
virtual input queue
    |
RunCPM
```

The desktop emulator is especially valuable for testing:

- profile #0..#9 selection;
- GENERAL/Codex history bank;
- RAW mode;
- Alt/Ctrl combinations;
- WordStar/Turbo Pascal/BASIC mappings;
- Home/End/Page/arrow translation;
- future backlight-key commands, where desktop may display an OSD/state change rather than controlling real brightness.

Physical panel-only commands should degrade into a logical emulator state rather than be silently ignored if their state matters to user behaviour.

---

## 12. History and application-profile testing

This must be a first-class emulator use case.

The current terminal history/profile behaviour is sufficiently complex that hardware-only validation is inefficient.

Provide deterministic tests for:

### 12.1 History bank behaviour

Test:

- selection of banks #0..#9;
- bank #9 GENERAL reservation for automated/Codex actions;
- line insertion;
- duplicate suppression;
- one-character/empty line exclusion;
- 100-line retention policy;
- previous/next history line;
- previous/next history bank;
- persistence write/read round trip;
- recovery from `.NEW` history file if applicable;
- RAW mode interaction with history capture;
- selecting a named bank while in RAW mode;
- active bank reporting.

### 12.2 Profile parsing

Feed fixture `7XTGVTRM.INI` data directly into:

```text
terminalConfigBegin()
terminalConfigFeed()
terminalConfigEnd()
```

and verify:

- valid profiles;
- malformed profile lines;
- unknown bindings;
- duplicate entries;
- fallback/default profile behaviour;
- named profile reporting;
- all currently documented application bindings.

### 12.3 Key translation

Script SDL-independent logical key events and verify final bytes delivered to CP/M.

Tests should not need a window for this.

Example fixture idea:

```text
input:
    ALT+2
    UP
    CTRL+]
    x

expected:
    active profile == 2
    emitted byte sequence == ...
    history state == ...
```

This permits high-confidence regression tests whenever application profiles change.

---

## 13. Codex-oriented testing and control

The desktop emulator should intentionally support automated operation by Codex without GUI clicking where possible.

### 13.1 Scripted input mode

Add a simple mode such as:

```text
7xtgv.exe --script tests/scripts/tp-edit.txt
```

or a stdin/control socket mechanism later.

A script should be able to express:

```text
TYPE "A2:"
ENTER
TYPE "TURBO"
ENTER
KEY CTRL-K
KEY CTRL-X
WAIT-IDLE
CAPTURE frame.png
ASSERT-CURSOR 12 5
ASSERT-HISTORY-BANK 2
```

The exact syntax can be designed later; the architectural requirement is more important than the syntax.

### 13.2 Deterministic byte-stream replay

Allow saved terminal-output streams to be replayed without RunCPM:

```text
7xtgv.exe --replay tests/streams/vt100-edit.bin
```

This directly tests parser + renderer behaviour.

Use recorded streams for:

- ANSI/VT100 tests;
- VT52 tests;
- DEC graphics;
- xterm-256 GCLS / Mandelbrot samples;
- cursor/save/restore sequences;
- scrolling-region cases;
- malformed/unsupported escape sequences.

### 13.3 Headless mode

Provide a mode that does not create an SDL window:

```text
7xtgv.exe --headless --replay ... --dump-rgb565 frame.raw
```

and ideally:

```text
--dump-png frame.png
```

PNG support may use a small dependency or test-side converter; it is not necessary for the first renderer core.

### 13.4 Frame hashes

For exact deterministic tests, compare the RGB565 framebuffer hash:

```text
SHA256(framebuffer) == expected
```

This is more reliable than screenshot visual comparison.

Reference PNGs are still useful for human review.

### 13.5 Terminal-state assertions

Expose test-only inspection helpers for:

- cursor x/y;
- terminal mode ANSI/VT52;
- current SGR state;
- scroll margins;
- active history/profile bank;
- RAW state;
- logical terminal cells;
- dirty-cell count;
- current virtual queues;
- xterm-256/GCLS armed state.

Do not expose these through the production UI unless useful; they may live behind a test build flag.

### 13.6 Codex workflow goal

The desired routine should become:

```text
edit terminal code
    |
build Windows emulator
    |
run tests under Wine
    |
feed scripted CP/M interaction
    |
capture framebuffer/state
    |
compare expected output
```

instead of:

```text
edit
compile firmware
flash MCU
open serial helper
manually reproduce sequence
inspect/photo TFT
```

Physical hardware remains essential for final validation of MCU/display timing and panel-specific behaviour, but no longer for every parser/UI iteration.

---

## 14. Deterministic rendering tests

The desktop shim should make it possible to test the **same actual terminal renderer** used by hardware.

Minimum frame-level regression set:

1. clean boot screen;
2. ANSI colour test;
3. DEC special graphics border test;
4. VT52 cursor-position test;
5. scrolling-region test;
6. reverse/underline/bold rendering;
7. cursor visible/hidden;
8. manual viewport/pan indicator where applicable;
9. history/profile popup/OSD;
10. GCLS xterm-256 1-pixel sample;
11. Brot-BS reference frame;
12. Brot-PS reference frame;
13. future native 7xtgv modes 0/1/2;
14. future tiled compositor/taskbar layout.

The emulator should use the same QL font data and palette tables as MCU builds, not copied/re-exported desktop versions.

---

## 15. Presentation timing and dirty updates

Separate terminal state updates from physical SDL presentation.

Terminal/parser state may update immediately per byte, but SDL `Present` should be rate-limited/coalesced.

A sensible desktop policy is approximately one presentation every 10-16 ms while dirty, with immediate present when idle if required.

This mirrors an important lesson already visible in RunVT: presenting/synchronising the physical window on every terminal character can unnecessarily throttle output.

Keep these concepts separate:

```text
terminal modifies logical framebuffer
    !=
SDL displays logical framebuffer
```

This also models our MCU goal of separating terminal/compositor work from expensive TFT transfer operations.

---

## 16. RunCPM execution speed

Do not artificially emulate MCU speed by default.

The desktop reference build should normally run RunCPM as fast as practical.

Optional later switches:

```text
--z80-mhz 4
--z80-mhz 20
--unlimited
```

may be useful for behaviour/timing demos, but parser/render correctness must not depend on them.

Performance measurements from this desktop build must not be confused with MCU Z80-equivalent benchmark results.

---

## 17. Storage architecture

Desktop RunCPM should preferably use normal host files/directories directly.

Avoid emulating `SdFat` merely to preserve source shape.

However, terminal profile/history storage needs a platform-neutral boundary so the same semantic code is tested.

Recommended split:

```text
history/profile parser + serialiser
    portable

file open/read/write/rename/remove
    MCU implementation: SdFat / SD_MMC
    desktop implementation: stdio / filesystem
```

Preserve atomic-history-save semantics where currently expected (`.NEW` then rename).

This is important because desktop tests should validate persistence logic, not bypass it.

---

## 18. Backlight controls in emulator

There is no physical TFT backlight on desktop.

Still retain the logical command/state so unified keystrokes behave consistently.

Possible emulator behaviours:

- show current brightness percentage in title/status OSD;
- optionally multiply presented RGB values by a visual brightness factor;
- 0% can blank presentation while retaining framebuffer state;
- do not alter underlying framebuffer/hash when testing terminal rendering unless explicitly testing brightness presentation.

Thus framebuffer correctness remains independent of display-brightness simulation.

---

## 19. Future native 7xtgv compositor

The desktop emulator should be designed as the reference environment for the planned native 7xtgv machine, not only today's VT terminal.

Future layers may include:

```text
7xtgv native parser/protocol
        |
terminal / tiled GUI state
        |
compositor
        |
Arduino_GFX-compatible drawing surface
        |
RGB565 framebuffer
        |
SDL2 or physical TFT
```

When system taskbar/windowing modes arrive, implement them once in the shared compositor and expect identical logical output on SDL and MCU targets.

This is one of the strongest reasons not to write a separate SDL terminal renderer.

---

## 20. Suggested source layout

Do not force this exact naming, but preserve these boundaries:

```text
src/7xtgv-desktop/
    main.cpp
    desktop_runtime.cpp
    desktop_runtime.h

    shims/
        Arduino.h
        Arduino_GFX_Library.h

    platform/
        sdl_display.cpp
        sdl_display.h
        sdl_input.cpp
        sdl_input.h
        host_storage.cpp
        host_storage.h

    tests/
        streams/
        scripts/
        expected/
```

Shared terminal sources should remain in their existing canonical location or later move into a neutral shared-core directory if that reduces duplication.

Do not copy `terminal.cpp`, fonts, palettes, history code or application-profile code into the desktop directory.

A source move/refactor is acceptable only when it makes the shared boundary cleaner; avoid a large reorganisation as the first step.

---

## 21. First implementation milestones

### Milestone 1 — host renderer proof

- Take the existing test `Arduino_GFX_Library.h` framebuffer stub as reference.
- Extend it to a selectable 480x320 logical framebuffer.
- Compile current terminal renderer/parser on desktop without SDL.
- Feed a known terminal byte stream.
- Dump framebuffer/hash.
- Verify one expected frame.

Success means the real terminal code can render without Arduino hardware.

### Milestone 2 — SDL2 viewer

- Add SDL2 window.
- Upload RGB565 framebuffer to texture.
- nearest/integer scaling;
- quit handling;
- basic refresh throttling.

Success means a replay stream produces the expected visible 7xtgv screen.

### Milestone 3 — desktop keyboard

- Map SDL key events into unified 7xtgv key events.
- Validate ANSI/VT52/application-profile behaviour.
- Test RAW/history bank controls.

### Milestone 4 — integrated RunCPM

- Compile RunCPM into the same executable.
- Use direct virtual console calls.
- Replace Arduino storage with desktop host storage.
- Boot CP/M from project drive tree.

Success means `7xtgv.exe` opens directly into our exact CP/M/7xtgv machine.

### Milestone 5 — automated Codex harness

- headless mode;
- replay mode;
- scripted key/input mode;
- framebuffer capture/hash;
- terminal-state assertions;
- history/profile persistence fixtures.

### Milestone 6 — MinGW static Windows package

- cross-build on Linux;
- static SDL2 if practical;
- run native Windows smoke test;
- run Wine smoke test;
- publish portable ZIP/single executable development artifact.

---

## 22. Acceptance tests for first useful release

A first useful desktop release should demonstrate all of these:

- boots RunCPM without serial hardware;
- renders current QL-font 7xtgv terminal correctly;
- ANSI mode works;
- VT52 mode works;
- normal CP/M keyboard input works;
- profile switching works;
- history banks work and persist;
- RAW mode works;
- Turbo Pascal/MBASIC/BBC BASIC profile interactions work at least as current hardware does;
- GCLS / current xterm-256 pixel rendering works;
- Brot-BS and Brot-PS show the same logical pixels as hardware;
- framebuffer can be captured deterministically;
- at least one headless regression suite runs without opening a window;
- Windows binary runs natively;
- same Windows binary runs under Wine;
- no PTY/ConPTY is required for integrated RunCPM mode.

---

## 23. Things explicitly out of scope for the first version

- complete Arduino framework emulation;
- complete Arduino_GFX API emulation;
- SPI/panel-controller electrical emulation;
- exact MCU timing emulation;
- touch-panel emulation unless a current 7xtgv feature needs it;
- serial-port terminal mode;
- TCP terminal server/client;
- RunVT parser integration;
- Sixel merely because RunVT supports it;
- ReGIS;
- installer/MSI;
- native Linux build unless it comes nearly for free;
- fancy resizable/reflowing desktop terminal behaviour.

---

## 24. Key design rule for Codex

When implementing this plan, preserve the following test:

> If a rendering/parser/history/application-profile bug is fixed in shared 7xtgv code, the same fix must affect both the desktop emulator and MCU/TFT build without duplicating the fix.

If desktop implementation begins accumulating separate terminal semantics, stop and move the boundary lower.

The intended distinction is:

```text
shared:
    terminal semantics
    terminal state
    fonts/palettes
    compositor
    history/profiles
    unified key logic
    RGB565 drawing behaviour

platform-specific:
    SDL window/event pump
    TFT/Arduino_GFX physical transport
    keyboard hardware acquisition
    filesystem/SD implementation
    timers/reboot/quit
    backlight hardware
```

---

## 25. Final architectural summary

The preferred reference model is:

```text
                         RunCPM
                            |
                    virtual console API
                            |
                     real 7xtgv core
          parser / profiles / history / compositor
                            |
                 Arduino_GFX-facing renderer
                            |
             +--------------+---------------+
             |                              |
          MCU build                    desktop build
             |                              |
       real Arduino_GFX             Arduino_GFX shim
             |                              |
      physical TFT bus              RGB565 framebuffer
                                            |
                                           SDL2
                                            |
                                   Windows / Wine window
```

RunVT remains useful as a compact, proven source of SDL/Windows build and event-loop ideas, but **7xtgv itself remains the only terminal machine in the desktop emulator**.

The desktop executable should ultimately be treated as the project's software reference implementation: useful to users, useful under Wine, but even more importantly useful to us and to Codex for rapid deterministic development and regression testing without touching USB serial hardware for every iteration.
