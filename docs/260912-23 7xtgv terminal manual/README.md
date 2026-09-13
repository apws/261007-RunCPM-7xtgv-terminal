# 7xtgv RunCPM terminal manual

This is the practical manual for the current ESP32-S3 RunCPM terminal: the TFT
terminal, its PC Konsole/Kitty mirror, persistent command histories, and the
ten application profiles.  The active device configuration is loaded once at
boot from `A0:7XTGVTRM.INI`.

## Starting the PC mirror

From the project directory, launch either:

```sh
tools/kon.sh
tools/kit.sh
```

Both attach to one `tmux` session named `7xtgv-cpm`.  It owns the USB serial
device, so do not run a second serial terminal beside it.  A USB reconnect can
briefly blink the PC screen while the complete terminal snapshot is redrawn.
This mirror-only refresh preserves a retained xterm256 image on the TFT, which
remains usable throughout.

`Ctrl+] q` leaves the host bridge.  `Ctrl+]` followed by `Ctrl+]` sends a real
literal Ctrl+] to CP/M.

## Global terminal keys

The keys below are intercepted by the 7xtgv terminal.  They do not become CP/M
input unless a profile deliberately maps them that way.

| Key | Action |
| --- | --- |
| `Ctrl+] 1` | Select ANSI/VT100 interpretation. |
| `Ctrl+] 2` | Select VT52 interpretation. |
| `Ctrl+] r` | Repaint normal terminal text on the TFT and PC. This also ends an active xterm256 plot. |
| `Ctrl+] g` | GCLS: arm the direct xterm256 graphics canvas below terminal cells. |
| `Ctrl+] a` / `e` | Move the 80-column viewport to its left / right edge. |
| `Ctrl+] h` / `l` | Pan that viewport four columns left / right. |
| `Ctrl+] p` / `n` | Previous / next history entry of the active effective bank. |
| `Ctrl+] [` / `]` | Select preceding / next application profile. |
| `Ctrl+] #` | Toggle `## RAW` direct-input mode. |
| `Ctrl+] x` | Send the selected profile's configured exit command. |
| `Ctrl+] c` | Send the selected profile's configured clear command. |

The PC key tables provide the more natural shortcuts:

| Key | Action |
| --- | --- |
| `Alt+0` … `Alt+9` | Select history/application profile `#0` … `#9`; its `#n name` label appears briefly. Top-row and numeric-keypad digits both work. On the Czech layout the host bridge recognizes `+ěščřžýáíé` as the top-row profile positions. |
| `Alt+PgUp` / `Alt+PgDn` | Previous / next profile, with immediate label redraw. |
| `Alt+Space` | Show the currently selected profile without changing it. |
| `Alt+Up` / `Alt+Down` | Profile-level alternate history controls where configured. |
| `Alt+Esc` | Configured profile exit. |
| `Alt+Backspace` | Configured profile clear. |
| `Alt+Shift+Backspace` | GCLS / xterm256 canvas clear. |
| `Alt+Shift+3` | Toggle `## RAW` mode. |

`Alt+Esc` and `Alt+Backspace` are configured globally in `7XTGVTRM.INI` as
dual-Alt gestures.  Their generated commands are intentionally not saved in
history.

## Histories, banks, and the active line

There are ten named FIFO histories, each retaining at most 100 entered lines.
Single-character submissions are ignored.  Up and Down normally recall entries
directly; each profile can instead give those keys to the target application and
put history on Alt+Up/Alt+Down.

The histories live in `A0:7XTGVTRM.BUF`.  After two seconds without a newly
stored line the firmware atomically rewrites that ordinary text file through
`7XTGVTRM.NEW`; history therefore survives normal resets.  It can be inspected
or repaired from CP/M if required.

Profile selection is explicit and persistent, but the RunCPM CCP prompt
(`A0>`, `AF>`, and similar) silently uses effective bank `#0` so shell commands
do not pollute an application history.  The selected bank is restored when the
application prompt returns.  The cursor row is rendered with a pale-green
background; its cursor is a saturated green block.

`## RAW` bypasses profile/history translation for direct control of an
application.  It is the escape hatch for diagnostic tools and unusual full
screen programs.  The normal terminal mode is preferable for ordinary command
work.

## Application profiles

| Bank | Name | Select action | History and key behaviour |
| --- | --- | --- | --- |
| `#0` | CP/M | Sends `A0:` | Up/Down history. Left/Right/Home/End/Delete/Insert send the CCP/WordStar-style controls `^A`, `^F`, `^B`, `^W`, `^G`, `^V`. Clear is `CLS`. |
| `#1` | MBASIC | Sends `A7:` | Screen-line history with the local composer. Left=`rubout`, Right=`space`, Home=`L`, End=`X Esc`, Delete=`1D`; supports practical `EDIT n` editing. Exit is `system`; clear prints ANSI clear/home. |
| `#2` | TE | Sends `A0:` | Keeps TE's normal cursor movement: `^S`, `^D`, `^Q S`, `^Q D`, `^E`, `^X`; history is Alt+Up/Alt+Down. Page keys map to TE commands. |
| `#3` | TURBO | Sends `A2:` | ANSI cursor and six-key sequences are passed through; history is Alt+Up/Alt+Down. |
| `#4` | COMAL | Sends `A3:` | ANSI cursor and six-key sequences; history is Alt+Up/Alt+Down. Exit is `BYE`, clear is `PAGE`. |
| `#5` | BBC BASIC | Sends `A1:` | `>` prompt screen-line history, lowercase-friendly local build. Basic cursor/edit controls map to BBC's own controls; normal Up/Down are history and Alt+Up/Alt+Down remain BBC controls. Exit is `quit`; clear prints ANSI clear/home. |
| `#6` | DDT | Sends `A0:` | Up/Down history; other mapped editor keys are deliberately disabled. |
| `#7` | VTESTS MBASIC | Sends `A7:` | Same local-composer behaviour as MBASIC, for the VT emulation test environment. |
| `#8` | ANSI APP | No automatic drive change | Standard ANSI cursor/six-key traffic; history is Alt+Up/Alt+Down. |
| `#9` | GENERAL | No automatic drive change | General CP/M/WordStar-style controls and Up/Down history. Automated transfer tools reserve this profile. |

Profile drive switching is only a convenience command emitted after the short
`#n name` indicator.  The profile does not change CP/M's disk model or user
area; it simply sends the configured text, usually `A<n>:` followed by Return.

## Editing details

The normal terminal is a line model, not a generic editor.  It records a line
when Return completes it and can recall it into line-oriented interpreters.

MBASIC needs special handling because its `EDIT n` command enters a modal
editor.  The `MBASIC` and `VTESTS MBASIC` profiles use the local composer after
that transition: left/right/home/end, insert, delete, backspace, Enter, and
Esc/PgUp/PgDn behave as a modern local edit line while the required MBASIC
control sequence is sent beneath it.

BBC BASIC uses its visible `>` prompt as the active screen line.  It tracks
ordinary line edits and safely retypes recalled history using the profile's
configured clear/retype sequence.  This is why its controls differ from
MBASIC's.

## xterm256 graphics

`Ctrl+] g` or `Alt+Shift+Backspace` starts a local GCLS graphics layer.  The
next xterm256 indexed-color samples (`CSI 38;5;<index>m` plus byte values) are
drawn as RGB565 pixels directly on the TFT and are not mirrored to the PC.  The
normal terminal cell layer remains logically present and opaque text output can
paint over the image.  `BROT-BS` and `BROT-PS` demonstrate this path from A6.

Use the supplied submit scripts after selecting A6:

```text
A6>SUBMIT BROT-BS
A6>SUBMIT BROT-PS
```

The direct plot ends on SGR reset (`CSI 0m`).  Rows used by status text and the
active cursor are then repainted from terminal memory, including when the plot
was launched by `SUBMIT`; the remaining graphics canvas stays visible.  A
normal `CLS` clears the terminal cells, while `Ctrl+] r` explicitly returns the
screen to the normal terminal surface.

## Configuration reference

`A0:7XTGVTRM.INI` is read once at firmware boot.  Each `[bankN]` supports a
name, `on_select`, `exit_command`, `clear_command`, regular/Ctrl/Alt cursor and
six-key bindings, history controls, active-line/cursor RGB values, and narrow
editing options such as `screen_line`, `line_prefix`, `activate_prefix`,
`input_adapter`, and `history_retype_delay_ms`.

Binding values are `none`, `history_previous`, `history_next`, `send:<bytes>`,
or `send_cancel:<bytes>`.  Escape notation accepts `\e`, `\r`, `\n`, `\t`, `\\`,
and `\xNN`.  Edit the file on the SD card and reboot to load a changed profile.

The mirror and keyboard bridge are implemented by `tools/terminal.py`; Kitty
and Konsole mappings are in `tools/kitty.conf` and `tools/7xtgv-cpm.keytab`.
