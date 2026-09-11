# MBASIC terminal editing

The MBASIC used on the CP/M card is Microsoft BASIC-80 Rev. 5.30.  It reads
console characters itself; it does **not** use RunCPM's BDOS buffered line
editor.  Consequently the useful `#0 CP/M` cursor mappings (`^A`, `^F`, `^B`,
`^W`, and so on) do not apply to MBASIC.

## Confirmed on the 7xtgv ESP32-S3 target

- `#0 CP/M` maps cursor keys to RunCPM's CCP line editor and supports normal
  in-line cursor movement.
- `#1 MBASIC` initially maps the cursor and six-key block to `none` in
  `A0:7XTGVTRM.INI`.  This was intentional: blindly forwarding CCP controls
  changes MBASIC input semantics.
- Sending `Ctrl+A` while MBASIC is receiving an unsubmitted line produces its
  `! ` edit-mode prompt.  The observed `! ABC` was therefore successful entry
  to MBASIC's own editor, not a terminal failure.
- `Ctrl+H` / Backspace removes the previous character during ordinary input.
- Raw RunCPM `Ctrl+A` as a purported one-character-left key does not move an
  MBASIC command-line cursor.  It enters MBASIC edit mode instead.

## MBASIC's own edit mode

`Ctrl+A` enters Edit Mode for the line being typed, with the edit cursor at its
beginning.  Its commands are line-editor commands rather than terminal cursor
sequences:

| Desired action | MBASIC edit-mode input |
| --- | --- |
| Move left | `Backspace` / Rubout |
| Move right | `Space` |
| Move to end and insert | `X` |
| Insert at cursor | `I`, text, `Escape` |
| Delete at cursor | `D` |
| Save and exit | `Enter` |
| Discard changes | `Q` |

The MBASIC `EDIT <line>` command enters the same mode for a stored program
line.  `EDIT .` selects the current program line.

Reference: [Microsoft BASIC-80 Release 5.0 Reference Manual](https://deramp.com/downloads/mfe_archive/040-Software/Microsoft/Basic-80%20Interpreter/Microsoft_BASIC-80_Release_5-0_Manual.pdf), section 2.16 and input editing.  The Rev. 5.30 target uses this compatible
editor model.

## Consequence for 7xtgv profiles

An INI-only mapping cannot safely make MBASIC behave like a modern terminal:

- mapping Right to `Space` types a space outside MBASIC edit mode;
- mapping End to `X` types `X` outside edit mode;
- mapping Home to `Ctrl+A` enters edit mode, rather than only repositioning a
  terminal cursor.

The current conservative `#1 MBASIC` profile therefore leaves these keys
unbound and retains 7xtgv history recall on plain Up/Down.

## Implemented `EDIT n` adapter

`#1 MBASIC` now explicitly selects `input_adapter=mbasic_native`.  It is a narrow
terminal-side adapter, active only after the submitted line begins with
`EDIT `, ignoring case.  It does not alter ordinary MBASIC immediate input,
`INPUT`, or `INKEY$`.

After `EDIT n` is submitted, 7xtgv waits 100 ms for MBASIC to enter Edit Mode,
then sends its `L` edit command.  The delay is necessary: sending `L` in the
same input burst as Return can reach MBASIC before its command parser changes
state, leaving only the line number visible.  `L` displays the complete source
line and positions the native editor at its beginning.  The `EDIT n` command
itself is discarded from terminal history; the displayed and subsequently
edited source line becomes the entry.

| PC key, while in `EDIT` mode | MBASIC input | Result |
| --- | --- | --- |
| Left | Rubout | Move one character left |
| Right | Space | Move one character right |
| Home | `L` | Display/rest at line beginning |
| End | `X Escape` | Move to end |
| Delete | `1D` | Delete character at cursor |
| Backspace | Rubout, `1D` | Delete character before cursor |
| Enter | Enter | Save line and leave native edit mode |
| Escape | `Q` | Discard line and leave native edit mode |
| Page Up / Page Down | `Q` | Discard line and leave native edit mode |

The terminal updates its own current-history model in parallel with these
operations, so a saved edited line is recalled correctly.  The adapter resets
when the bank changes or the line is saved/cancelled.

Outside native Edit Mode, MBASIC profile Up/Down remain ordinary 7xtgv
history recall.  The adapter must intercept only the edit-only semantic keys,
never the history controls.

This remains MBASIC-specific.  It must stay opt-in in the profile, rather than
becoming a general CP/M or VT100 behavior.

## Local-compose `EDIT n` bridge (next stage)

The native editor is unsuitable for in-place display, but an observed `X`
immediately after `EDIT n` renders the full numbered source line.  The planned
bridge will use that **single `X` only**, capture the rendered line into
`input_adapter=local_compose`, and let the terminal edit/re-submit it.  Do not
inject Escape: raw testing showed it has unreliable consequences in this state.
The native-editor exit transition remains deliberately uncommitted until it is
verified on the target.

The MBASIC profile's unified actions are deliberately lower-case because this
MBASIC accepts lower-case input: `system` exits and
`print chr$(27);"[2J";chr$(27);"[H"` clears/home the ANSI terminal.
