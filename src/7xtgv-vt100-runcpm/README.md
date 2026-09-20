# 7xtgv VT100/VT52 + RunCPM

An isolated RP2350-Touch-LCD-2 sketch: CP/M on core 1, USB keyboard input and
an ANSI/VT52 screen on core 0. The TFT displays a manually panned 52×24 viewport
of an 80×24 terminal. The PC displays the complete interpreted 80×24 screen.

The original demos are separate. This terminal uses the 6x10 QL font and
7xtgv's four paper and sixteen ink colors, but its visible-cell arrays are
not yet the native four-plane 7xtgv design. Sidecar flags supply underline,
reverse, and DEC graphic selection. It does not run the cheers animation.

## Source layout

The Arduino sketch and RunCPM-derived implementation headers stay at the
sketch root for upstream comparison. Compiled terminal parser, profiles,
history, runtime renderer, QL font, graphics setup, shared-palette symlink,
and terminal-only helpers live in `src/terminal/`; Arduino compiles
that `src/` tree recursively. The board storage, backlight, and reboot
headers are under `hardware/arduino_7xtgv/`. The palette's single shared
definition remains in `../7xtgv-palette.h`. Native quad-plane 7xtgv is not
implemented here yet; the current Arduino_GFX painter belongs to the terminal.

## Build and flash

From this directory:

```sh
/home/falken/.local/bin/arduino-cli compile --profile rp2350-touch-lcd-2 --output-dir ../../build/build-vt100-runcpm .
```

The profile pins Arduino-Pico 5.4.4, Arduino_GFX 1.6.4, SdFat 2.3.0, RP2350A,
150 MHz, 16 MB flash, and a conservative 12 MHz dedicated SPI1 SD bus. Display
geometry is landscape 320×240 at rotation 3; font cells are 6×10, with an
eight-pixel full-height grey gutter on the left at the default viewport (and
on the right after the right-edge pan). Its RGB value is configured by
`viewport_gutter_rgb` in `A0:7XTGVTRM.INI`.

Copy `../../build/build-vt100-runcpm/7xtgv-vt100-runcpm.ino.uf2` to the RP2350 BOOTSEL
volume. The device restarts automatically. Keep the existing CP/M SD card
inserted. No SD files are changed by firmware installation.

The prior firmware remains at
`../../build/build-runcpm-backlight/7xtgv_RunCPM.ino.uf2` for rollback.

## PC keyboard and mirror

Enlarge Kitty or Konsole to at least 80×24. The recommended launchers create or
attach to one shared `tmux` session, so one wrapper owns the serial port while
several desktop views can show the same CP/M terminal:

```sh
tools/kit.sh
tools/kon.sh
```

Only the first launcher starts `terminal.py`; later launchers attach to its
`7xtgv-cpm` tmux pane. A read-only monitor and an explicit stop are available:

```sh
tools/cpm-session.sh --monitor
tools/cpm-session.sh --stop
```

Codex can inspect the same pane with `tmux capture-pane -pt 7xtgv-cpm` and send
input with `tmux send-keys -t 7xtgv-cpm`, without opening the serial device a
second time. The session disables tmux's usual Ctrl+B prefix because HOME sends
Ctrl+B to RunCPM. Direct single-client use remains available:

```sh
python3 tools/terminal.py --port /dev/ttyACM0
```

There are no Python package dependencies. The helper uses Linux termios at
115200, asserts DTR, disables local echo and software/hardware flow control,
and restores your PC terminal when it exits. It uses the PC alternate screen.
Keyboard input is 7-bit terminal input; the mirror uses UTF-8 for DEC graphics.
If the ESP32 USB serial device briefly disappears, the existing helper stays in
its tmux session, reopens `/dev/ttyACM0` when it returns, and requests a fresh
mirror snapshot. It does not blank the PC terminal or invalidate an xterm256
plot on the TFT.

If access is denied, grant temporary access to the current device:

```sh
sudo setfacl -m u:"$USER":rw /dev/ttyACM0
```

That grant lasts only until USB re-enumeration, including every ESP32 flash.
Apply it after flashing and before starting the shared session. The helper takes
exclusive access; do not run picocom or a second direct helper alongside it.

| Key | Action |
| --- | --- |
| Plain/Ctrl/Alt arrows and six-key block | Active application-profile binding |
| Alt+0 ... Alt+9 | Select named history/key profile #0 ... #9; show `#n name` for 1 s |
| Alt+Space | Show the active `#n name` for 1 s without changing the bank or current line |
| Alt+# or Ctrl+] then # | Toggle temporary `## RAW` input bypass |
| Alt+PageUp / Alt+PageDown | Select previous/next history list |
| Ctrl+Shift or Alt+Shift+0 ... 9 | Backlight: off, then coarse 20 ... 100% |
| Ctrl+Shift or Alt+Shift+PageUp / PageDown | Increase / decrease one of 32 brightness levels |
| Ctrl+] then a / e | Same edge jumps; works without Alt mappings |
| Ctrl+Shift or Alt+Shift+Home / End | Global viewport jump to left / right edge |
| Ctrl+] then 1 / 2 | Select ANSI / VT52 locally |
| Ctrl+] then g or Shift+Alt+Backspace | GCLS: arm one local xterm-256 pixel plot without clearing the TFT |
| Ctrl+] then r | Refresh PC mirror and TFT |
| Ctrl+] then p / n | Previous/next history line |
| Ctrl+] then [ / ] | Previous/next history list |
| Ctrl+] then b then 0 ... 9 / + / - | Backlight fallback: coarse set / precise raise / lower |
| Ctrl+] then q | Exit PC helper; leave CP/M running |
| Ctrl+] then x / c | Active profile's unified Exit / Clear command |
| Alt+Esc / Alt+Backspace | Same unified Exit / Clear commands in Kitty/Konsole |
| Ctrl+] twice | Send literal Ctrl+] to CP/M |

Each bank holds 100 lines and ignores empty, one-character and immediately
duplicated entries. Arrow and six-key-block behavior is translated
by the selected application profile. The firmware reads `A0:7XTGVTRM.INI` once
at boot; see `../../docs/260910-01 terminal-application-profiles.md`.
Bank `#9 GENERAL` is reserved for Codex and other automated terminal actions,
so those commands do not consume the user's active application history.

`## RAW` keeps the selected bank underneath it but sends ordinary input bytes
straight to the CP/M console. It bypasses history capture, recall, application
key bindings, and future application adapters. Toggle it again to restore the
underlying bank; selecting `#0` through `#9` exits RAW and selects that bank.
`Alt+Space` reports `[#n name]` normally or `[## RAW / #n]` in RAW mode.
Private host controls are `CSI > 15 v` (toggle), `CSI > 16 v` (on), and
`CSI > 17 v` (off). `CSI > 63 ; n v` controls the backlight: `n=0..32` sets
off or an exact one of 32 active levels; `n=33` raises and `n=34` lowers one
level. The remembered non-off value is safely written to `[global]`
`backlight_level` in `7XTGVTRM.INI` after input settles; zoom changes also
persist as `view_zoom` in the active HAL's display section. These sequences remain
reserved even in RAW mode.

Panning is manual, with only left/right edge jumps, even when the cursor moves
off-screen. The hidden-content side is a full-height configurable-grey gutter,
not an unused controller margin: left at the default edge, right after panning.
A 6×6
red square at the top-left is the short-latched SD activity indicator. It is a
fixed display overlay, so scrolling cannot move it; retained plots suppress it.

Status overlays are target-specific display policy, not a HAL or pin default.
Before placing a future activity LED, pan mark, or taskbar indicator on another
module, check its physical resolution, controller offsets/rotation, and any
reserved non-scrolling area. Also check the board schematic: a nominal LED pin
may instead control TFT backlight or share display wiring, and must not be
driven as a disk LED.

The helper does not reset terminal mode on connection. Use `--mode ansi` or
`--mode vt52` to select explicitly. Mode switches preserve the screen and cursor
but discard an incomplete escape sequence; switch before launching the program.
The firmware boots in ANSI mode without waiting for USB. Reconnect to get a
fresh mirror of the current screen. CP/M resumes neither a new session nor a new
boot merely because the helper reconnects.

### Application key mappings

The helper's Ctrl+] shortcuts always work. The project Kitty and Konsole
profiles contain the translated CP/M/WordStar-style keyboard mappings plus the
profile-selection controls. Launch them with `tools/kit.sh` and
`tools/kon.sh` as shown above.

The launchers resolve their own directory and pass arguments to the first
`terminal.py` instance. Arguments supplied by later attaching clients are
ignored because the serial owner is already running. With no arguments they
use `/dev/ttyACM0`, making both scripts directly launchable from a file manager.

`tools/kitty-cpm.conf` retains a separately named copy of the same mappings.
Plain PageUp/PageDown are application-profile keys.

```sh
kitty --config tools/kitty-cpm.conf picocom /dev/ttyACM0
```

For Konsole, use **Edit Current Profile → Keyboard → Edit** in a separate
profile and set these keys to send the listed strings (Konsole uses `\E` for
ESC). Remove conflicting mappings in that profile if necessary.

| Key | Konsole output | Bytes |
| --- | --- | --- |
| Ctrl+] then `a` / `e` | private helper action | Viewport left/right edge |
| Ctrl+Shift or Alt+Shift+Home / End | global viewport action | Viewport left/right edge |
| Ctrl+Shift or Alt+Shift+0 ... 9 | local terminal action | Backlight off / coarse 20 ... 100% |
| Ctrl+Shift or Alt+Shift+PageUp / PageDown | local terminal action | Raise / lower one of 32 levels |

## Supported terminal subset

### ANSI / VT100-style (default)

- CR, LF/VT/FF, BS, HT, BEL, SI/SO. HT starts every eight columns; ESC H sets a
  stop, CSI g clears the current stop, CSI 3 g clears all stops.
- CSI A/B/C/D, E/F, G, H/f, d: cursor movement/positioning; CSI J/K modes 0/1/2:
  erase. CSI 3 J scrollback clearing is ignored because there is no scrollback.
- ESC D/M/E: index, reverse index, next line. CSI top;bottom r: scroll margins.
  CSI n L/M inserts/deletes lines from the cursor through the bottom margin.
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
alternate screen, 132 columns, double-sized lines, UTF-8 input, 8-bit C1 controls
other than CSI, mouse, automatic pan, application function-key remapping, or
binary transfer.
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

`GCLS` is the private `ESC[>4v` terminal control, not a standard xterm
sequence. Its extended form is `ESC[>4;zoom;x;yv`, where zoom is 1..4 and x/y
are absolute physical panel pixels from the display's top-left corner,
independent of terminal zoom, gutter width, and viewport pan. Thus `x=0` may
paint over the gutter; the requested coordinates are not multiplied by zoom.
GCLS itself does not clear or redraw the TFT or PC mirror; use `CLS`
separately when a fresh text screen is wanted. While armed, `CSI 38;5;n m` followed by
one printable byte becomes one local RGB565 sample block; CR/LF ends each
logical plot row and `CSI 0m` ends the transaction. The samples never enter
terminal text RAM or the PC mirror. `BROT-BS` and `BROT-PS` can therefore
remain unchanged even though RunCPM masks the former program's `0xDB` sample
byte to `[`. Normal opaque terminal output after the final reset sequence
overwrites the temporary image. The first later terminal scroll retires the
plot and repaints the gutter without a full-screen clear.

The direct RP2350/ST7789 target renders one 6×10 scratch tile at a time and
sends it to the display. Its write-only wiring cannot read native pixels back.
The JC8048W535 ESP32-S3 target instead uses its documented rotated PSRAM canvas
workaround; see [its implementation note](../../docs/260912-12%20JC8048W535%20software%20rotation%20and%20framebuffer%20implementation.md).
A future debug-only USB request can capture a deterministic 80×24 RGB image by
replaying terminal cell buffers through the same renderer; it must use private
diagnostic framing separate from CP/M and the normal screen mirror.

## Tests

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -pthread src/terminal/terminal.cpp src/terminal/history_profiles.cpp tests/test_terminal.cpp -o /tmp/7xtgv-terminal-tests
/tmp/7xtgv-terminal-tests
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -pthread -Itests/stubs src/terminal/terminal.cpp src/terminal/history_profiles.cpp tests/test_runtime.cpp -o /tmp/7xtgv-runtime-tests
/tmp/7xtgv-runtime-tests
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -pthread -Itests/stubs src/terminal/terminal.cpp src/terminal/history_profiles.cpp tests/test_plot_position.cpp -o /tmp/7xtgv-plot-position-tests
/tmp/7xtgv-plot-position-tests
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

Tests exercise fragmented/malformed escapes, erase/wrap/margins, line
insertion/deletion, colors, charsets, replies, VT52, manual pan, keyboard
timeouts, 200,000 hostile bytes,
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

### Persistent terminal history

The terminal keeps its ten 100-line history banks in `A/0/7XTGVTRM.BUF`, beside
`7XTGVTRM.INI`.  It loads once at boot and, after two seconds without a new
stored line, writes and rereads a complete `7XTGVTRM.NEW` on the RunCPM/SD-owner
core. Rotation keeps the previous generation in `7XTGVTRM.BAK` until the new
buffer is installed. A CRC-32 footer rejects torn v2 writes at boot, with
fallback through `.NEW` and `.BAK`; existing v1 files remain readable. Each bank
is `[0]` through `[9]`; each retained command begins with `>`, making the file
safe to edit on a host while the device is off.
