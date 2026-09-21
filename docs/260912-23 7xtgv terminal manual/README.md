# 7xtgv RunCPM terminal manual

This is the practical manual for the current 7xtgv RunCPM terminal: the TFT
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

### XMODEM over the mirror

`A0:XMODEM.COM` v2.7 transfers binary files over the same USB console. Run
`CONSOLE8` first: XMODEM needs all eight bits preserved. Then start the CP/M
side and open the host panel with `Ctrl+Shift+X` (or the portable fallback
`Ctrl+] m`). The panel accepts a typed host path and owns the serial stream
until completion; protocol bytes are never rendered as CP/M text.

| Direction | Start in CP/M first | Choose in panel |
| --- | --- | --- |
| Host file to CP/M | `XMODEM DEST.BIN /R /X0` | `S`, then source pathname |
| CP/M file to host | `XMODEM SOURCE.BIN /S /X0` | `R`, then new destination pathname |

The host receiver writes to `name.part` first and refuses to overwrite either
that partial file or an existing destination. On success the file is renamed;
after either outcome the mirror reconstructs its normal CP/M screen. The panel
uses CRC XMODEM with 128-byte blocks and retry/ACK handling. `Esc` cancels
before the transfer begins.

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
| `Ctrl+] p` / `n` | Previous / next history entry of the active effective bank. A token before the cursor filters entries beginning with it. |
| `Ctrl+] [` / `]` | Select preceding / next application profile. |
| `Ctrl+] #` | Toggle `## RAW` direct-input mode. |
| `Ctrl+] x` | Send the selected profile's configured exit command. |
| `Ctrl+] c` | Send the selected profile's configured clear command. |
| `Ctrl+] m` | Open the host-only XMODEM transfer panel. |
| `Ctrl+] b 0` … `9` | Backlight off, then coarse 20% … 100%. |
| `Ctrl+] b +` / `-` | Raise / lower one of 32 backlight levels. |

### Output interpretation modes

There are exactly two persistent terminal-output interpretations: ANSI/VT100
(the normal default, including the implemented xterm colour extensions) and
VT52. `Ctrl+] 1` / `2` select them; the host bridge can also select one at
connection with `--mode ansi` or `--mode vt52`. An application can request the
standard DEC transition itself: `CSI ? 2 h` selects ANSI/VT100, `CSI ? 2 l`
selects VT52, and VT52 `ESC <` returns to ANSI/VT100.

`## RAW` is only an input-routing state, application profiles are keyboard
translation choices, and GCLS/xterm256 plot is a temporary graphics canvas;
none is a third persistent terminal emulation mode. Application cursor/keypad
flags are likewise negotiated submodes within ANSI/VT100, not separate
terminal types.

The PC key tables provide the more natural shortcuts:

| Key | Action |
| --- | --- |
| `Alt+0` … `Alt+9` | Select history/application profile `#0` … `#9`; its `#n name` label appears briefly. Top-row and numeric-keypad digits both work. On the Czech layout the host bridge recognizes `+ěščřžýáíé` as the top-row profile positions. |
| `Alt+PgUp` / `Alt+PgDn` | Previous / next profile, with immediate label redraw. |
| `Alt+Space` | Show the currently selected profile without changing it. |
| `Alt+Up` / `Alt+Down` | Profile-level alternate history controls where configured. |
| `Alt+Esc` | Configured profile exit. |
| `Alt+Backspace` | Configured profile clear. |
| `Alt+Shift+Backspace` | Arm GCLS / xterm256 plotting without clearing the screen. |
| `Alt+Shift+3` | Toggle `## RAW` mode. |
| `Ctrl+Shift+0` … `9` | Backlight off, then coarse 20% … 100%. |
| `Ctrl+Shift` or `Alt+Shift` + `PgUp` / `PgDn` | Raise / lower one of 32 backlight levels. |
| `LAlt+Ctrl+Shift+PgUp` / `PgDn` | Increase / decrease glyph zoom. |
| `LAlt+Ctrl+Shift+1` / `2` | Select ANSI/VT100 / VT52 terminal output interpretation. |
| `Ctrl+Shift` or `Alt+Shift` + `Home` / `End` | Global viewport jump to the left / right 80-column edge. |

`Alt+Esc` and `Alt+Backspace` are configured globally in `7XTGVTRM.INI` as
dual-Alt gestures. Their generated commands, and the Ctrl+Shift viewport
jumps, are handled before application-profile bindings and are not saved in
history. The last non-off brightness and selected glyph zoom are safely
persisted in the INI file after input settles.

## Histories, banks, and the active line

There are ten named FIFO histories, each retaining at most 255 entered lines.
Single-character submissions are ignored. Up and Down normally recall entries
directly; each profile can instead give those keys to the target application and
put history on Alt+Up/Alt+Down. If there is a token immediately before the
cursor (characters back to space, tab, or line start), recall filters for
history entries beginning with that token, wraps in both directions, replaces
only that token while retaining the surrounding line, and adds one trailing
space. A filtered completion inside a nonempty prefix (for example `10 PR`) is
never stored as a new history item; an edited completion at line start remains
an ordinary storable REPL command. Tabs are accepted as existing input and
filter separators, but spaces are preferred for authored CP/M text.

The histories live in `A0:7XTGVTRM.BUF`.  After two seconds without a newly
stored line, the RunCPM/SD-owner core writes and rereads `7XTGVTRM.NEW`, then
rotates the prior generation through `7XTGVTRM.BAK`. Completed v2 files carry a
CRC-32 footer; boot rejects a torn copy and tries `.NEW` and `.BAK`. History
therefore survives normal resets without cross-core filesystem access. It can
be inspected or repaired from CP/M if required.

Profile selection is explicit and persistent, but the RunCPM CCP prompt
(`A0>`, `AF>`, `A>`, and similar) silently uses effective bank `#0` so shell commands
do not pollute an application history.  The selected bank is restored when the
application prompt returns.  The cursor row is rendered with a pale-green
background; its cursor is a mid-green block whose character ink remains the
same pale green.

`## RAW` bypasses profile/history translation for direct control of an
application.  It is the escape hatch for diagnostic tools and unusual full
screen programs.  The normal terminal mode is preferable for ordinary command
work.

## Application profiles

| Bank | Name | Select action | History and key behaviour |
| --- | --- | --- | --- |
| `#0` | CP/M | Sends `A0:` | Up/Down history. Left/Right/Delete/Insert use CCP/WordStar-style controls. Home/End are stable terminal-side start/end movements, avoiding RunCPM's context-sensitive `^B`/`^W` toggle/recall. Clear is `CLS`. |
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

## 7xtgv CP/M commands and user areas

This compact reference covers files added or adapted by this project. It does
not repeat ordinary CP/M programs and imported language packages; use `?` at
the CCP prompt for the internal CCP command list. An A0 command is found from
other user areas by this RunCPM CCP, while files it creates normally belong to
the current DU (for example `A6>`).

The internal `CCPP` command changes only the displayed prompt: `CCPP DD`
(default) shows decimal user numbers (`A15>`), `CCPP DH` shows hexadecimal
digits (`AF>`), and `CCPP D` shows only the drive (`A>`), as on a conventional
CP/M 2.2 prompt. Bare `CCPP` reports the mode. `USER` and drive/user commands
still select the same actual areas; prompt formatting does not change their
syntax. The setting survives ordinary program returns but resets on a full
RunCPM restart. This requires firmware containing the new internal command;
there is no `CCPP.COM` file to install.

| DU | 7xtgv additions or adapted launchers |
| --- | --- |
| A0 | `REBOOT`, `UNSUB`, `VTCPU`, `VTCPU8X`, `VTDRV`, `VTDRV8X`, `VTTERM`, `VTGET`, `VTPUT`, and shared `VTESTS.BAS`. `7XTGVTRM.INI` and history generations live here. |
| A1 | `BBCLOWER`, a project-built BBC BASIC accepting lowercase keywords and immediate commands only. |
| A2 | No project command; Turbo Pascal and `TINST` are supplied software. |
| A3 | No project command; the COMAL area is presently empty. |
| A4 | No project command; assembler/debugger tools are supplied software. |
| A5 | No project command; imported CP/M/CCP source and its build SUB files. |
| A6 | Graphics, timing, sync, and benchmark helpers; Brot scripts and sources. |
| A7 | Isolated duplicate `MBASIC` plus `VTESTS.BAS` workspace. |
| A8 | `VTDEMO.BAS` and `VTDEMO2.BAS`, the 7xtgv ANSI/colour demo programs. |
| A9 | No project command; Lua files are experimental supplied material. |
| AA | No project command; alternate MBASIC binaries. |
| AC | No project command; GEEK2350 source/archive material. |
| AD | No project command; disk-image archive only, not a device-sync target. |

### Shared A0 commands

| Command | Purpose |
| --- | --- |
| `REBOOT` | Schedules a whole-module reboot after a three-second notice. The serial mirror disconnects, so relaunch `tools/kon.sh` or `tools/kit.sh` afterwards. |
| `REFLASH` | First forces and verifies the same history save as `SYNC`, then emits the reserved host event `ESC[>64v`. It flashes nothing by itself; an attached `terminal.py` acts only when its target-specific `XTGV_REFLASH_COMMAND` is explicitly configured. |
| `ZOOM [1|2|3|4]` | Set absolute 1x…4x glyph zoom; no argument means 1x. Its source is A0 `ZOOM.ASM` and it requires firmware implementing private output `CSI >68;n v`. |
| `UNSUB` | Deletes a stranded `$$$.SUB` queue in the current DU without switching drive or user. It is silent if none exists. |
| `VTCPU` / `VTCPU8X` | Measure the RunCPM interpreter with the terminal active and paused. `8X` uses the 8080/8085 common instruction subset. |
| `VTDRV` | Plain invocation read-tests and verifies a retained 32 KiB `VTDRV.$$$` fixture. Run `VTDRV RW` once at a known-safe clock to create it. Options are case-insensitive. |
| `VTDRV8X` | Write, read, and verify 32 KiB in the current DU, then remove its temporary test file. Uses the 8080/8085 instruction subset. |
| `VTTERM` | Run the firmware-native terminal/parser benchmark. |
| `VTGET` / `VTPUT` | CP/M ends of the host `tools/cpm-transfer/transfer.py` protocol; normally use the host tool rather than type their protocol manually. |
| `MBASIC`, then `RUN "VTESTS"` | Start the shared terminal visual-test menu. A7 provides the same isolated pair. |

`UNSUB.ASM` is retained in the A0 source mirror; its assembled `UNSUB.COM` is
currently installed on the card and should be rebuilt/deployed after card
replacement.

### A6 graphics and timing helpers

| Command | Purpose |
| --- | --- |
| `GCLS [zoom [x y]]` | Arm local xterm256 plotting without clearing or redrawing the screen. `zoom` is 1…4; `x,y` are unscaled physical pixel coordinates from the display's top-left corner, independent of terminal zoom, gutter, or pan. Bare `GCLS` retains `ESC[>4v` and defaults to 1× at `(0,0)`. Use `CLS` separately when wanted. |
| `SYNC` | Forces the verified terminal-history save immediately. Use before a long graphics stream or likely hardware reset. |
| `VTPAD [n]` | Prints `n` CR/LF pairs; default is 18. It provides visible space where SUBMIT would discard empty script records. |
| `VTSTART` / `VTSTOP` | Save and report elapsed RunCPM uptime around a test, using `VTIME.DAT` in the current DU. |
| `SUBMIT BROT_BS` / `SUBMIT BROT_PS` | Recommended Brot launchers: clear, pad nine lines, start timing, sync history, arm GCLS, run the renderer, then report elapsed time. |
| `SUBMIT VTPARG n` | Argument-expansion test: runs `VTPAD n`. |

The current card contains the assembled A6 helpers. The source mirror retains
their `.ASM` sources (and, where available, `.HEX` files); after replacing a
card, rebuild missing `.COM` files in A6 with `ASM name` then `LOAD name`. The
two Brot renderers use underscores deliberately: TE treats `-` as an editing
command separator, so use `BROT_BS` and `BROT_PS`, not hyphenated names.

### Boot, AUTOEXEC, and pending SUBMIT work

`AUTOEXEC.TXT` is a RunCPM convenience, not a standard CP/M 2.2 CCP command.
It is a short first-boot command line at the SD-card filesystem root (outside
every A0--AF DU). For deliberate startup, it can contain `SUBMIT STARTUP`,
with ordinary `STARTUP.SUB` held in the intended CP/M DU.

`SUBMIT` builds the internal `$$$.SUB` queue. On boot the CCP can find such a
pending queue and resume it; that is useful after interruption but is not a
safe way to turn ordinary text prefixed with `$` into autorun. If an abandoned
queue causes surprising commands after boot, switch to its DU and run `UNSUB`.
A pending queue takes precedence over `AUTOEXEC.TXT`.

## xterm256 graphics

`Ctrl+] g` or `Alt+Shift+Backspace` starts a local GCLS graphics layer. The
extended private form is `CSI >4;zoom;x;y v`; `zoom` is 1…4 and `x,y` are
physical pixel coordinates from the display's top-left corner, not logical
text cells and not multiplied by zoom. The origin does not move with the
gutter or viewport; samples at `x=0` may cover the gutter. Each source sample becomes a `zoom × zoom` RGB565 block, clipped at the
physical client boundary. The next xterm256 indexed-color samples
(`CSI 38;5;<index>m` plus byte values) are drawn directly on the TFT and are
not mirrored to the PC. The normal terminal cell layer remains logically
present and opaque text output can paint over the image. The first later
terminal scroll retires the plot and restores the gutter without a full-screen
clear. `BROT_BS` and `BROT_PS` demonstrate the compatible bare path from A6.

Use the supplied submit scripts after selecting A6:

```text
A6>SUBMIT BROT_BS
A6>SUBMIT BROT_PS
```

The direct plot ends on SGR reset (`CSI 0m`).  Rows used by status text and the
active cursor are then repainted from terminal memory, including when the plot
was launched by `SUBMIT`; the remaining graphics canvas stays visible.  A
normal `CLS` clears the terminal cells, while `Ctrl+] r` explicitly returns the
screen to the normal terminal surface.

## Configuration reference

`A0:7XTGVTRM.INI` is read once at firmware boot. Its `[global]` section holds
the global exit/clear key policy, cursor blink, `viewport_gutter_rgb` (the
always-present full-height display gutter), `viewport_active_gutter_rgb`
(normally `active_line` when horizontal panning exists), and persisted
`backlight_level`. The matching `[display:HAL_ID]` section holds the persisted
`view_zoom` and per-zoom geometry. Each `[bankN]` supports a
name, `on_select`, `exit_command`, `clear_command`, regular/Ctrl/Alt cursor and
six-key bindings, history controls, active-line/cursor RGB values, and narrow
editing options such as `screen_line`, `line_prefix`, `activate_prefix`,
`input_adapter`, and `history_retype_delay_ms`.

Binding values are `none`, `history_previous`, `history_next`, `send:<bytes>`,
or `send_cancel:<bytes>`. Escape notation accepts `\e`, `\r`, `\n`, `\t`, `\\`,
`\xNN`, and `\^X` for an ASCII control character.

### Keyboard overrides in `7XTGVTRM.INI`

Put a binding under `[global]` to make it available in every profile, or under
`[bank0]` … `[bank9]` for one selected application profile. A bank-specific
binding wins over the global one; otherwise the profile's `input=` preset or
the terminal's default applies. These banks are terminal profiles, **not**
CP/M drives or user areas. Keep the section spelling `[bankN]` lowercase.

`input=` selects one of these keyboard-input presets. It is independent from
`mode=ansi|vt52`, which selects the **output** interpretation:

| `input=` value | Preset bindings |
| --- | --- |
| `custom` | Retains the normal/default bindings of the selected profile; add or replace individual bindings explicitly. |
| `ansi` | ANSI cursor/navigation sequences for arrows, Home/End, PgUp/PgDn, Insert/Delete, and their implemented Ctrl/Alt variants. |
| `vt52` | Arrows only: `Esc A`, `Esc B`, `Esc C`, `Esc D`. Other navigation keys are unbound unless explicitly configured. |
| `wordstar` | WordStar-style arrows `^E/^X/^S/^D`; Home/End `^Q^S/^Q^D`; PgUp/PgDn `^R/^C`; Insert `^V`; Delete `^G`. |

For every non-`custom` preset, `Alt+Up` and `Alt+Down` default to the
profile history previous/next actions. An explicit profile or global key
binding always wins. There are currently no `input=adm3a`, `input=mz821`, or
`input=fand` presets: configure such applications with `input=custom` and
specific bindings after their required input bytes are known.

`input_adapter=none|mbasic_native|local_compose` is separate again: it
selects narrowly scoped local line-edit handling, not a terminal protocol.
Likewise, `## RAW` is a temporary bypass of profile key translation rather
than an `input=` preset.

```ini
[global]
kp_decimal=send:.
ctrl_shift_f5=send:\eQ

[bank3]
ctrl_shift_f5=none
shift_num1=send:1
```

The left side is `[ctrl_][shift_][alt_]<key>`, with modifier words in exactly
that order and joined by underscores. Use `ctrl_shift_alt_f5`, not
`alt_ctrl_shift_f5`; names are case-insensitive. `alt_` is intended for LAlt.
RAlt/AltGr is reserved for the Czech layout's symbol entry, and is not a
separate INI modifier.

| Key group | Available base names |
| --- | --- |
| Cursor and editing | `left`, `right`, `up`, `down`, `home`, `end`, `insert`, `delete`, `page_up`, `page_down` |
| Common keys | `backspace`, `escape`, `enter`, `space`, `f1` … `f12` |
| Main number row | `num0` … `num9` (separate from the keypad) |
| Numeric keypad | `kp_decimal`, `kp_delete` (the latter is NumLock-off Delete) |
| Czech physical positions | `bs2`, `bs1` before Backspace; `p1`, `p2` after P; `l1`, `l2`, `l3` after L; `z1` before Z; `m1`, `m2`, `m3` after M |
| Letters | `a` … `z`, only with Ctrl or Alt; plain and Shift-only letters remain normal typing |

Any subset of the three modifier words is syntactically accepted for the
non-letter names. Plain and Shift-only `bs1` and `l3` are deliberately excluded
because these are dead-accent positions on the current Czech keyboard: their
normal composition must continue to work. `num1` … `num0` send ASCII digits
by default even when the unshifted Czech keycap produces `+ěščřžýáíé`;
`kp_decimal` sends `.` by default even when the host emits `,`. Shifted
main-row digits are separate names such as `shift_num1`.

The right side has five forms:

| Value | Effect |
| --- | --- |
| `send:<bytes>` | Send the specified bytes to CP/M. |
| `send_cancel:<bytes>` | Send bytes and discard the current terminal history line. |
| `history_previous` / `history_next` | Browse the effective history bank. |
| `none` | Explicitly suppress that key. |

`<bytes>` may contain up to 16 decoded bytes; `\e` is Escape, `\r` is Return,
and `\xNN` inserts a byte in hexadecimal. `\^K` sends Ctrl-K (0B); the
backslash-caret consumes exactly one character. Thus `\^K\^D` sends two
control bytes, while `\^K^D` sends Ctrl-K followed by printable `^D`.
Bare `^K` stays printable. The full convention is `\^@` = NUL, `\^A` …
`\^Z` = 01 … 1A (lowercase letters also accepted), `\^[` = Escape,
`\^\` = 1C, `\^]` = 1D, `\^^` = 1E, `\^_` = 1F, and `\^?` = DEL.
Invalid forms such as `\^5` are rejected; `\e` remains the clearer spelling
for Escape in terminal sequences. Write comments on their own lines
beginning with `;` or `#`—not after a binding value, where they would be read
as part of that value. A duplicate key or an excess over 128 sparse overrides
across the whole configuration reports a load error; the first accepted
binding remains in effect.

The device image stamped `260920-1628` predates `\^` support. The corrected
`260920-1816` image is staged but not yet flashed; continue using `\xNN` in
the device-side INI until that reflash.

INI recognition and host delivery are distinct. The supplied Konsole/Kitty
keymaps translate only physical combinations with a safe known fallback;
terminal-owned shortcuts (for example Alt+number profile selection and
Ctrl+Shift+number backlight), ambiguous AltGr combinations, unmodified dead
accents, and some less-tested punctuation combinations are left to the host.
Konsole's key table does not encode left-versus-right Alt explicitly, and a
desktop shortcut can consume a chord before this terminal sees it. In
particular, a correctly spelled INI binding does not guarantee that every
host keyboard will deliver that physical combination. Ordinary unbound keys
retain their host output; pasted text is not remapped.

`7XTGVTRM.INI` is deliberately CP/M-text friendly: its loader accepts CR, LF,
or CR/LF line endings and treats a CP/M Ctrl+Z (`1Ah`) as logical end of file.
It can therefore be edited as `A0:7XTGVTRM.INI` with a CP/M editor, then
reloaded by `REBOOT`. A delayed, verified transaction updates only the active
HAL's `view_zoom` and global `backlight_level` after a zoom/brightness change;
other settings and other HAL sections remain user-authored. The saved zoom is
applied before the CP/M boot banner. The host transfer tool's `--cpm-text` mode writes the same
CR/LF-plus-Ctrl+Z convention. This applies only to the INI file: do not edit
the checksummed native history files (`7XTGVTRM.BUF`, `.NEW`, or `.BAK`) from
CP/M.

The mirror and keyboard bridge are implemented by `tools/terminal.py`; Kitty
and Konsole mappings are in `tools/kitty.conf` and `tools/7xtgv-cpm.keytab`.

`A0:REFLASH` is deliberately a guest-to-host capability request, not a CP/M
firmware writer. Its event is removed from the displayed terminal stream. On a
development PC, set `XTGV_REFLASH_COMMAND` in `tools/terminal.conf` to a
non-interactive uploader for one specific target; the bridge releases its
serial lock, runs that command, then reconnects and requests a mirror snapshot.
Leaving the variable unset is the safe default and reports the request without
changing the device. Assemble `REFLASH.ASM` to install `REFLASH.COM`.
The mirror selects an explicit `XTGV_TARGET` in `tools/terminal.conf` and
uses `tools/reflash.py` to load that target's profile, default port and upload
method from `tools/targets/<target>.json`. Override a changed USB node with
`XTGV_PORT`. The current development target is `jc8048w550`; change it before
moving to another module. A `bootsel-uf2` target intentionally refuses this
automatic host event until its BOOTSEL/UF2 flow is explicitly configured.
