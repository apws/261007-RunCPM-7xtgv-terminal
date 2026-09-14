# 7xtgv backlight brightness control implementation plan

Date: 2026-09-13/14
Status: signed implementation plan
Scope: MCU/TFT 7xtgv terminal targets

This plan began as the ChatGPT/Lyra backlight proposal added in commit
`4e0b104`. It incorporates the subsequent design discussion about direct CSI
input, Czech PC keyboards, Konsole/Kitty mappings and a future custom XITU-T
keyboard. The keyboard architecture notes are included to explain the input
boundary; implementing a general keyboard protocol is not part of this pass.

## 1. Goal and fixed user behavior

Add a board-independent physical TFT-backlight control below the terminal
renderer. It is a terminal-global facility, independent of the active CP/M
application profile, ANSI/VT52 mode, history bank and xterm256 plot state.

The user-facing levels are fixed:

| Level | Backlight duty |
|---:|---:|
| 0 | off |
| 1 | 10% |
| 2 | 20% |
| 3 | 30% |
| 4 | 40% |
| 5 | 50% |
| 6 | 60% |
| 7 | 70% |
| 8 | 80% |
| 9 | 90% |

Level 9 intentionally remains 90%, not full duty. This preserves the ten-state
scheme agreed during planning.

The current boot behavior remains unchanged: the firmware initially writes
duty 150 on the 0..255 Arduino scale. The logical initial level is 6. Therefore
the first increase changes to level 7 and the first decrease changes to level 5;
selecting level 6 explicitly changes the nearly identical duty from 150 to 153.

Increment/decrement saturates at 0 and 9 and never wraps. Level 0 turns off only
the LED backlight. The LCD controller, framebuffer, terminal, CP/M runtime,
serial mirror and keyboard input remain alive, so any brightness command can
restore the light.

Brightness is not persisted in this version. Every reboot restores the existing
duty-150 startup behavior.

## 2. Common backlight HAL

Keep GPIO/PWM details out of terminal parsing and rendering. Add a small common
API near the display/HAL setup:

```cpp
void xtgvBacklightInit();
void xtgvBacklightSet(uint8_t level);       // public level 0..9
void xtgvBacklightAdjust(int8_t delta);     // used with -1 or +1
uint8_t xtgvBacklightGet();
bool xtgvBacklightSupported();
```

Implementation rules:

- `xtgvBacklightInit()` applies the existing duty 150 and records level 6.
- `xtgvBacklightSet()` clamps its API argument to 0..9 and applies the duty
  table `{0,26,51,77,102,128,153,179,204,230}`.
- `xtgvBacklightAdjust()` saturates before calling the setter.
- Invalid private-protocol parameters are rejected before calling the API; a
  malformed command must not accidentally clamp to level 9.
- The common implementation uses Arduino `analogWrite(pin, duty)` with an
  8-bit 0..255 duty. Both pinned build environments support this interface.
- Preserve explicit pin polarity in the hardware layer. Both currently active
  LCD targets are active-high; do not assume this for future targets.
- Initialization or PWM failure must leave the terminal operational and report
  the backlight facility as unsupported where the platform can detect failure.

Current and future hardware knowledge:

| Target/module | Backlight | Status for this pass |
|---|---:|---|
| Waveshare RP2350 Touch LCD 2 | GPIO15 | implement and hardware-test first |
| JC8048W535 ESP32-S3 | GPIO1 | implement, compile-test, hardware-test when connected |
| JC8048W550 ESP32-S3 | GPIO2 | known future target; not currently buildable here |
| JC8012P4A1C ESP32-P4 | GPIO23 | known future target; not currently buildable here |
| Waveshare RP2350 LCD 1.47 | GPIO21 | dormant target; test when restored |
| Waveshare ESP32-C6 LCD 1.47 | GPIO22 | dormant target; test when restored |
| RP2350 AMOLED targets | no LED backlight | unsupported until panel luminance is designed |

The Waveshare RP2350 Touch LCD 2 schematic confirms that GPIO15 drives the
LCD backlight transistor. The Arduino-Pico core implements `analogWrite()` with
hardware PWM; Arduino-ESP32 exposes a compatible 0..255 `analogWrite()` API over
LEDC. References:

- <https://files.waveshare.com/wiki/RP2350-Touch-LCD-2/RP2350-Touch-LCD-2.pdf>
- <https://arduino-pico.readthedocs.io/en/stable/analog.html>
- <https://docs.espressif.com/projects/arduino-esp32/en/latest/api/ledc.html>

Do not introduce platform-version-specific LEDC calls until a supported target
actually requires them. If that happens, contain compatibility handling in the
HAL rather than the terminal code.

## 3. Canonical terminal protocol

The stable interface presented to PC front ends, custom keyboards and CP/M
applications is a private CSI command:

```text
CSI > 63 ; n v
```

`CSI` is the byte sequence `ESC [`.

| `n` | Action |
|---:|---|
| 0..9 | set absolute backlight level |
| 10 | increase by one level |
| 11 | decrease by one level |
| any other value | ignore |

Command 63 is selected instead of the preliminary proposal's command 40 because
`CSI > 40 v` is already the canonical plain-PageUp input token. A parameterized
form could be distinguished, but a separate namespace is clearer and safer.

Examples:

```text
ESC [ > 63 ; 0 v       backlight off
ESC [ > 63 ; 7 v       level 7 / 70%
ESC [ > 63 ; 10 v      increase one step
ESC [ > 63 ; 11 v      decrease one step
```

The same semantic action must be reached through both terminal directions:

```text
PC/custom keyboard input -> keyboard CSI decoder -> common backlight action
CP/M program output       -> terminal CSI parser -> common backlight action
```

The command is always consumed locally. It must not be displayed, added to
history or delivered as CP/M keyboard input. It remains reserved and usable in
`## RAW` mode. Extend the RAW-mode control recognizer for the longer
parameterized sequence while preserving its timeout/pass-through behavior for
ordinary or incomplete escape sequences.

No query/reply command is added. A future capability query may expose
brightness and other terminal-local state, but it is outside this pass.

## 4. Signed PC keyboard mappings

Konsole and Kitty should send the final private CSI directly. The primary path
must not synthesize `Ctrl+] b n` first.

| Physical PC chord | Direct bytes/action |
|---|---|
| `Ctrl+Shift+0` .. `9` | `CSI >63;0v` .. `CSI >63;9v` |
| `Alt+Shift+0` .. `9` | same absolute-level commands |
| `Ctrl+Shift+PgUp` | `CSI >63;10v`, increase |
| `Alt+Shift+PgUp` | same increase command |
| `Ctrl+Shift+PgDn` | `CSI >63;11v`, decrease |
| `Alt+Shift+PgDn` | same decrease command |
| `Ctrl+Shift+Home` | global viewport left edge |
| `Alt+Shift+Home` | global viewport left edge |
| `Ctrl+Shift+End` | global viewport right edge |
| `Alt+Shift+End` | global viewport right edge |

Both modifier families are intentional. `Ctrl+Shift` is convenient with the
right-hand modifier keys on Petr's keyboard; `Alt+Shift` remains a parallel
choice. Konsole/Kitty normally match modifier classes rather than reliably
distinguishing left and right keys.

Resolve existing collisions explicitly:

- Plain `Alt+0..9` continues to select history banks 0..9.
- Existing redundant `Alt+Shift+0..9` bank mappings are replaced by brightness.
- `Alt+Shift+3` no longer doubles as the normative RAW toggle.
- The Czech-layout RAW chord remains `LAlt+RAlt+X`, which produces Meta-`#` in
  the current environment; `terminal.py` translates `ESC #` to `CSI >15v`.
- `Ctrl+] #` remains a debug/fallback RAW toggle.
- `RAlt+3` has also been observed to work in the present XKB/Qt/Konsole stack,
  but it is not a portable or normative binding until its actual byte path is
  captured and documented.

Desktop-wide KDE/Linux shortcuts may intercept a chord before Konsole or Kitty.
The MCU cannot resolve such a collision. Avoid dangerous desktop conventions,
especially anything close to `Ctrl+Alt+Delete`, and test every primary chord in
the actual desktop environment.

Update all maintained host definitions together:

- Konsole `7xtgv-cpm.keytab`
- Kitty `kitty.conf` and `kitty-cpm.conf`
- `terminal.py` fallback help and tests
- keyboard cheatsheet and terminal manual

Longer term, define one authoritative action/sequence registry from which host
maps, custom-keyboard constants, tests and manual tables can be generated or at
least mechanically verified. That broader configurability work is not required
to ship backlight control.

## 5. `Ctrl+]` is a fallback, not the friendly primary UI

The preliminary plan described `Ctrl+] b 7` as human-friendly. That is not true
on a Czech PC keyboard: `]` is normally reached through `RAlt+G`, so the prefix
itself is already a multi-modifier gesture.

Retain the helper because it is useful for diagnostics and byte-only terminals:

```text
Ctrl+] b 0 .. 9        set absolute brightness
Ctrl+] b +             increase
Ctrl+] b -             decrease
```

`terminal.py` converts these to the same `CSI >63;n v` commands. An invalid
continuation is passed through without silently losing input. A doubled
`Ctrl+]` continues to send a literal `0x1D` to CP/M.

This helper is not required by the MCU protocol and is not required for a custom
keyboard that can emit CSI directly.

## 6. Current PC mirror/input boundary

The firmware remains the authoritative terminal. It interprets CP/M output,
owns the terminal buffer and TFT framebuffer, and emits an ANSI text mirror.

`terminal.py` is a transport/session helper, not a second terminal emulator. It
currently:

- exclusively opens and configures the 115200-baud USB serial device;
- moves PC stdin and MCU mirror output through nonblocking queues;
- applies the `Ctrl+]` fallback and Czech Alt-number-row compatibility mapping;
- detects USB device-node replacement and reconnects after reset or flashing;
- requests/retries a complete text snapshot after connection;
- manages DTR, raw TTY mode, the alternate screen and clean restoration;
- keeps input usable during a hardware outage and records unexpected failures.

Konsole or Kitty renders the received ANSI mirror. Direct xterm256 pixels are
drawn only on the TFT and are not reconstructed by `terminal.py`.

A future SDL2 7xtgv emulator/front end may replace this PC development stack,
but it should use the same canonical terminal actions and private CSI protocol.

## 7. Custom XITU-T keyboard note (adjacent future design)

7xtgv is embedded-first. A future compact, calculator-like XITU-T keyboard does
not need USB HID or a host PC. Its controller may resolve its own physical keys,
modifiers and layers, then send ordinary bytes and canonical 7xtgv CSI directly
over UART, SPI or I2C:

```text
physical XITU-T chord
    -> keyboard layout/layer resolution
    -> canonical character, application-key CSI or local-action CSI
    -> MCU terminal input
```

Recommended first implementation:

- printable keys emit the configured CP/M character bytes;
- control chords emit their resulting control byte where appropriate;
- navigation/application keys emit the existing canonical 7xtgv key tokens so
  the active MCU application profile can translate them;
- terminal-local keys emit private CSI such as `CSI >63;7v` directly;
- modifiers pressed alone emit nothing;
- the keyboard does not generate `Ctrl+]` merely to reach a local action.

This byte/CSI mode is deliberately simple and equally usable over UART, SPI or
I2C. No USB round trip, HID host stack, press/release packet protocol or desktop
layout is required.

A later interchangeable-keyboard design may instead carry layout-neutral key
events (physical key ID, press/release and modifier bitmap) and place the keymap
in the terminal MCU. USB HID usage numbers could be reused only as an internal
key-ID vocabulary without requiring USB transport. Do not design or implement
that protocol until central remapping, rollover or interchangeable keyboards
actually require it.

The manual must distinguish these layers:

1. physical gesture or XITU-T key/layer;
2. XKB/Konsole/Kitty or custom-keyboard mapping;
3. canonical byte/private CSI token;
4. semantic terminal action;
5. local-MCU versus CP/M destination.

"Global" means that the MCU action bypasses application profiles. It does not
mean a raw UART byte stream can preserve physical Alt/Ctrl/Shift state; the
keyboard or desktop front end must resolve those modifiers before transmission.

## 8. Implementation placement

Use one backlight state and one action path:

```text
PC chord --------------------> private CSI --+
Ctrl+] diagnostic fallback --> private CSI --+--> local action dispatcher
XITU-T keyboard -------------> private CSI --+             |
CP/M application output -----> private CSI --+             v
                                                     backlight HAL
                                                          |
                                                          v
                                                     GPIO/PWM duty
```

The CP/M emulator core does not own brightness. Core 0, which already owns the
terminal, display and backlight, applies every brightness action. Do not access
display GPIO from the RunCPM core or from the host helper.

## 9. Tests and acceptance

### Native/parser tests

Prove that:

1. initialization writes duty 150 and reports logical level 6;
2. absolute levels 0..9 produce the exact duty table;
3. increment/decrement starts from level 6 and saturates at both endpoints;
4. `CSI >63;0v`, `CSI >63;9v`, `CSI >63;10v` and `CSI >63;11v` work;
5. invalid/malformed parameters make no change;
6. commands work from PC input and CP/M output;
7. commands remain local and never enter guest input/history or visible text;
8. commands remain available in RAW, ANSI, VT52 and plot states;
9. unrelated CSI, profile keys, snapshot and RAW controls remain unchanged;
10. an unsupported backlight target keeps the terminal operational.

### Host tests

- Verify both Ctrl+Shift and Alt+Shift absolute and adjustment mappings.
- Verify both modifier families for viewport Home/End.
- Verify `Ctrl+] b 0`, `b 9`, `b +` and `b -` generate the same canonical CSI.
- Verify invalid fallback input is preserved.
- Check Konsole and both Kitty maps for duplicate/conflicting bindings.
- Keep a diagnostic test capable of capturing PC input bytes for uncertain
  Czech XKB/Qt paths such as `RAlt+3`.

### Builds

- Run the native terminal, runtime, helper and integration tests.
- Compile the pinned `rp2350-touch-lcd-2` profile.
- Compile the pinned `jc8048w535` profile.
- Do not mark dormant targets supported merely because their pin is known.

### RP2350 hardware smoke test

1. Flash the current 2-inch RP2350 module and completely relaunch the visible
   Konsole mirror.
2. Check 0 fully extinguishes the backlight while CP/M and the mirror continue.
3. Check 1..9 are stable and visibly monotonic, without flicker or audible PWM.
4. Check both absolute-shortcut families.
5. Check both PgUp/PgDn adjustment families and endpoint saturation.
6. Check both Home/End viewport families.
7. Check the Czech RAW chord and its documented fallback.
8. Check brightness changes do not disturb TFT, USB, SD or the disk indicator.
9. Reboot and confirm the previous duty-150 startup appearance returns.

## 10. Explicit non-goals

Do not add in this pass:

- brightness persistence in flash, NVS or the SD-card INI;
- automatic idle dimming or ambient-light control;
- fades or animations;
- gamma/perceptual calibration UI;
- brightness query/reply protocol;
- AMOLED controller-specific luminance;
- a general MCU-side physical-key configuration schema;
- a binary UART/SPI/I2C keyboard-event protocol;
- SDL2 emulator implementation.

These may be layered later without changing the signed 0..9 user model or the
canonical private CSI action.
