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

## Possible future adapter

A dedicated, explicitly enabled MBASIC adapter can provide PC-style
Left/Right/Home/End.  It must maintain an MBASIC-edit-mode state in the 7xtgv
input layer:

1. On the first navigation request, enter MBASIC Edit Mode with `Ctrl+A` and
   position its cursor according to the locally tracked input line.
2. While active, translate Left/Right to Rubout/Space, Home to the editor's
   beginning command, End to `X`, and Delete to `D`.
3. End the state when Enter saves the line or `Q` discards it.
4. Keep generic history capture separate, since MBASIC editor commands do not
   describe the final edited line as ordinary printable input.

This is a terminal feature specific to MBASIC, not a general CP/M or VT100
behavior.  It should remain disabled unless the `#1 MBASIC` profile explicitly
enables it, so program `INPUT`, `INKEY$`, and other immediate character reads
remain predictable.
