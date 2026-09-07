# 7xtgv VT100/VT52 + RunCPM

An isolated RP2350-Touch-LCD-2 sketch: CP/M on core 1, USB keyboard input and
an ANSI/VT52 screen on core 0. The TFT displays a manually panned 52×24 viewport
of an 80×24 terminal. The PC displays the complete interpreted 80×24 screen.

The original demos are separate. This project retains the original 7xtgv fonts,
three packed screen planes (paper glyph, ink glyph, GGPPIIII attributes), and
four original white/pastel paper colors. Sidecar flags supply underline,
reverse, and DEC graphic selection. It does not run the cheers animation.

## Build and flash

From this directory:

```sh
/home/falken/.local/bin/arduino-cli compile --profile rp2350-touch-lcd-2 --output-dir ../build-vt100-runcpm .
```

The profile pins Arduino-Pico 5.4.4, Arduino_GFX 1.6.4, SdFat 2.3.0, RP2350A,
150 MHz, and 16 MB flash. Display geometry is landscape 320×240 at rotation 3;
font cells are 6×10, with an eight-pixel indicator strip on the right.

Copy `../build-vt100-runcpm/7xtgv-vt100-runcpm.ino.uf2` to the RP2350 BOOTSEL
volume. The device restarts automatically. Keep the existing CP/M SD card
inserted. No SD files are changed by firmware installation.

The prior firmware remains at
`../build-runcpm-backlight/7xtgv_RunCPM.ino.uf2` for rollback.

## PC keyboard and mirror

Enlarge kitty or Konsole to at least 80×24, close any other serial connection,
and run:

```sh
python3 tools/terminal.py
```

The helper finds the unique `usb-Generic_RP2350*-if00` device under
`/dev/serial/by-id/`. To choose explicitly:

```sh
python3 tools/terminal.py --port /dev/ttyACM0
```

Or launch from the project directory in a new window:

```sh
kitty python3 tools/terminal.py
konsole -e python3 tools/terminal.py
```

There are no Python package dependencies. The helper uses Linux termios at
115200, asserts DTR, disables local echo and software/hardware flow control,
and restores your PC terminal when it exits. It uses the PC alternate screen.
Keyboard input is 7-bit terminal input; the mirror uses UTF-8 for DEC graphics.

If access is denied, grant temporary access to the current device:

```sh
sudo setfacl -m u:"$USER":rw /dev/ttyACM0
```

That grant lasts only until USB re-enumeration. It is not automatically applied
by the helper. The helper takes exclusive access; close it before picocom.

| Key | Action |
| --- | --- |
| Alt+Home | TFT columns 1–52 |
| Alt+End | TFT columns 29–80 |
| Alt+Left / Alt+Right | Pan four columns, bounded at either edge |
| Ctrl+] then a / e | Same edge jumps; works without Alt mappings |
| Ctrl+] then h / l | Same left/right pan |
| Ctrl+] then 1 / 2 | Select ANSI / VT52 locally |
| Ctrl+] then r | Refresh PC mirror and TFT |
| Ctrl+] then q | Exit PC helper; leave CP/M running |
| Ctrl+] twice | Send literal Ctrl+] to CP/M |

Panning is manual, even when the cursor moves off-screen. The strip shows the
visible fraction of the page. White marks at its top/bottom mean the cursor is
left/right of the viewport. A yellow flash means BEL; a persistent red mark
means a terminal-response overflow from a program that is not reading input.

The helper does not reset terminal mode on connection. Use `--mode ansi` or
`--mode vt52` to select explicitly. Mode switches preserve the screen and cursor
but discard an incomplete escape sequence; switch before launching the program.
The firmware boots in ANSI mode without waiting for USB. Reconnect to get a
fresh mirror of the current screen. CP/M resumes neither a new session nor a new
boot merely because the helper reconnects.

### Explicit Alt-key mappings

The helper's Ctrl+] shortcuts always work, so desktop shortcuts need not be
changed. For native Alt keys, kitty can load this project's optional config:

```sh
kitty --config tools/kitty.conf python3 tools/terminal.py
```

For Konsole, use **Edit Current Profile → Keyboard → Edit** in a separate
profile and set these keys to send the listed strings (Konsole uses `\E` for
ESC). Remove conflicting mappings in that profile if necessary.

| Key | Konsole output | Bytes |
| --- | --- | --- |
| Alt+Home | `\E[1;3H` | `1b 5b 31 3b 33 48` |
| Alt+End | `\E[1;3F` | `1b 5b 31 3b 33 46` |
| Alt+Left | `\E[1;3D` | `1b 5b 31 3b 33 44` |
| Alt+Right | `\E[1;3C` | `1b 5b 31 3b 33 43` |

## Supported terminal subset

### ANSI / VT100-style (default)

- CR, LF/VT/FF, BS, HT, BEL, SI/SO. HT starts every eight columns; ESC H sets a
  stop, CSI g clears the current stop, CSI 3 g clears all stops.
- CSI A/B/C/D, E/F, G, H/f, d: cursor movement/positioning; CSI J/K modes 0/1/2:
  erase. CSI 3 J scrollback clearing is ignored because there is no scrollback.
- ESC D/M/E: index, reverse index, next line. CSI top;bottom r: scroll margins.
- CSI ?6 h/l: origin mode; CSI ?7 h/l: delayed autowrap; CSI ?25 h/l: cursor.
- ESC 7/8 and CSI s/u: cursor/rendition/charset save and restore.
- ESC ( B / ESC ) B: ASCII G0/G1; ESC ( 0 / ESC ) 0: DEC graphics; SI/SO select
  G0/G1. ASCII outside the DEC replacement range is unchanged.
- SGR 0, 1/22, 4/24, 7/27, 30–37/39, 40–47/49, 90–97, 100–107.
- CSI 5 n → CSI 0 n. CSI 6 n → CSI row;column R, origin-relative within scroll
  margins when origin mode is enabled. Columns always refer to the logical 80.
- CSI c and ESC Z → CSI ?1;0 c (VT100 base identification).
- CSI ?1 h/l and ESC =/> select application/normal cursor and keypad modes.
- ESC # 8 alignment pattern; ESC c reset; CSI ?2 l enters VT52 mode.

### VT52-compatible mode

ESC A/B/C/D, H, I, J, K; ESC Y followed by row+32 and column+32; ESC Z → ESC / Z;
ESC F/G select special graphics/ASCII; ESC =/> select application/numeric keypad;
ESC < returns to ANSI. Like a VT100's VT52 compatibility mode, graphics use the
DEC special-graphics set rather than the original VT52 character-generator ROM.

Arrows are translated from PC CSI/SS3 keys into the selected terminal's format.
SS3 numeric-keypad sequences are translated when the PC actually sends them;
numeric digits arriving as ordinary ASCII remain digits. Modified keys other
than the reserved pan keys pass through, as do unrecognized escape sequences.
Enter maps to CR, Backspace/DEL to BS. Incomplete keyboard escapes flush after
50 ms. Normal CP/M control keys (including Ctrl+C and Ctrl+S/Q) pass through.

### Deliberate limits

This is a documented subset, not a complete certified VT100. No scrollback,
alternate screen, 132 columns, double-sized lines, UTF-8 input, 8-bit C1 controls,
mouse, automatic pan, application function-key remapping, or binary transfer.
Unsupported CSI commands are ignored as units. OSC/DCS/PM/APC payloads are
consumed without storage through BEL or ESC-backslash. CAN/SUB cancel partial
control sequences. Parameter storage is capped at 16, with values capped at
9999. Bytes above 127 become `?` rather than indexing past the font.

The VT107 palette uses the original bright normal inks. Their dark/bold
companions use the former experimental normal colors. Visual tuning sets
normal gray to (100,100,100), normal silver to (230,230,230), normal yellow to
(230,230,50), and dark yellow to (200,194,0). The dedicated black entry
remains (0,0,0) for bold gray and default console text.
ANSI 30–37 select the normal entries; ANSI 90–97 select the corresponding dark
entries directly, and SGR 1 also selects the dark companion. ANSI red/green/blue paper requests map explicitly
to the corresponding pastel paper; unsupported paper requests use the closest
available pastel. Default ink
is black and paper white; bold selects the darker companion ink. In particular,
ANSI black backgrounds cannot be reproduced by the four pastel paper choices.
Reverse swaps the resolved colors at rendering time. PC RGB values match the
palette; TFT RGB565 quantization and physical panel response differ slightly.
DEC borders are explicit 6×10 masks; pi and pound use compact approximations,
and unsupported DEC control-picture glyphs use `?`.

## Implementation and transport

Core 0 owns USB, terminal state, display SPI, and backlight. Core 1 owns CP/M,
SdFat, and SD SPI1. An acquire/release readiness flag prevents CP/M startup until
display initialization has completed. Core 1 has a separate stack. Console calls
use `virtualAvailable`, `virtualRead`, and `virtualWrite`; CP/M input echo also
passes through the terminal. No USB writes occur on the CP/M core.

The virtual serial link uses fixed 4 KiB input / 16 KiB output SPSC byte rings
with lock-free acquire/release index publication, rather than the small hardware
FIFO. Storage is static and never allocated during terminal operation.
CP/M output backpressures when necessary. The terminal keeps draining output
while CP/M input is full and renders at most 12 dirty cells per loop. Pending
keyboard input stays in USB buffers; large pastes beyond host/device buffering
are not a file-transfer protocol and have no end-to-end delivery guarantee.

Terminal replies have a separate 1 KiB staging queue, drained before new keys.
A malicious/non-reading application can exhaust finite reply buffering: the
whole additional reply is dropped and the red indicator latches. CP/M output
continues to be parsed and drawn, avoiding a cyclic input/output deadlock.

USB output consists only of generated ANSI screen updates, never raw CP/M
queries. This avoids duplicate terminal answers and mirrors VT52 programs on
modern PC terminals. Updates are snapshotted one row at a time into an 8 KiB
buffer, transmitted only when USB has room, and coalesced while the PC is slow.
A connection or refresh requests a full screen snapshot. Cursor updates are
included; no device-identification or cursor-report queries are sent to the PC.
The PC mirror's font need not visually match the QL font on the TFT.

The firmware does not retain a full TFT framebuffer: it renders one 6×10 cell
scratch tile at a time and sends it to the display. The ST7789 wiring is also
write-only, so a native capture cannot read pixels back from the panel. A future
debug-only USB request can therefore capture a deterministic 80×24 RGB image by
replaying the terminal cell buffers through the same renderer; that request must
use a private diagnostic framing separate from CP/M and the normal screen mirror.

## Tests

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -pthread terminal.cpp tests/test_terminal.cpp -o /tmp/7xtgv-terminal-tests
/tmp/7xtgv-terminal-tests
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -pthread -Itests/stubs terminal.cpp tests/test_runtime.cpp -o /tmp/7xtgv-runtime-tests
/tmp/7xtgv-runtime-tests
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

Tests exercise fragmented/malformed escapes, erase/wrap/margins, colors,
charsets, replies, VT52, manual pan, keyboard timeouts, 200,000 hostile bytes,
and one million ordered bytes through the actual inter-core ring implementation
while the other direction is full. The runtime tests use fake USB/GFX peripherals with the actual firmware code
to exercise slow/disconnected mirrors, saturation, reconnect snapshots, and
bounded row rendering. They also produce framebuffer previews under `/tmp`.
The helper tests prefix handling and CP/M control-byte preservation, plus an
end-to-end pseudo-TTY session and terminal restoration. LeakSanitizer needs an environment without ptrace.

On hardware: boot without USB, connect the helper, run `DIR VT*.*`, MBASIC, PRINT 2+2,
then SYSTEM. Test panning while idle and during output; disconnect/reconnect;
verify TFT remains lit. `tests/VTESTS.BAS` is the single MBASIC visual suite.
Run `MBASIC`, then `RUN "VTESTS"`. Select 1–8 for ink, paper, attributes,
DEC graphics, VT52, manual pan, the combined ANSI test, or the Mixworx code sample. Individual tests
stay visible until Enter. A runs all eight automatically, then waits for Enter;
Q returns to CP/M. The ink page shows all eight normal/dark pairs side by side.
BASIC-80 5.30 uses internal numbered GOSUB routines, not named procedures.
This replaces the separate BASIC fixtures; no other BAS files are loaded.
See VALIDATION.md for checks actually performed on the firmware build.

References: [DEC VT100 programmer guide](https://vt100.net/docs/vt100-ug/chapter3.html),
[xterm control sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html),
and the installed Arduino-Pico 5.4.4 multicore/USB implementation.

The Mixworx code page is an illustrative LSB-like procedure, not an executable
MBASIC program. It uses gray for ordinary text, SGR 1 + gray (black) for
keywords, normal red for strings/numbers, normal blue for punctuation/operators,
normal green for comments, and normal magenta for identifiers. All 18 source
rows fit within 52 columns. Green/cyan normal inks were reduced about 5%;
dark inks except black were raised about 5% for the latest visual tuning.
