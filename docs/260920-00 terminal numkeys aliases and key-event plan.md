# `numkeys` aliases and configurable key events — 260920 addendum

Status: **design proposal, awaiting Petr's review**. This corrects and extends
the [260919 keyboard plan](260919-19%20terminal%20keyboard%20physical%20keys%20and%20sparse%20overrides%20plan.md).
No code, host keymap, firmware candidate, or device INI has been changed for
this proposal.

Later 260920 clarification: this character-alias idea is **not needed** for
the current Czech Konsole layout: an isolated key-event test recognized all
ten main-row keys and the keypad separately. Keep `numkeys` only as a possible
future fallback where a host cannot supply distinct key events. See the refined
[`260920-01 main-row and keypad identity plan.md`](260920-01%20main-row%20and%20keypad%20identity%20plan.md).

## Confirmed observation and the immediate answer

Petr tested the unmodified Czech keyboard in nano under Konsole. Nano receives
and displays the Czech characters, including the two-press dead accents on
`BS1` and `L3`. The current Konsole → Python mirror → device input path passes
the characters onward; in CP/M, the non-ASCII/dead-key group gives no usable
result. This does **not** show that Konsole lost the physical keystrokes.

Konsole has key events and can translate named Qt keys plus Ctrl/Shift/Alt
into byte sequences using `.keytab`. It also has a documented `KeyPad` keytab
condition, so it **can distinguish the main row from the numpad** before
translation. Our **current** Python PTY bridge sees only Konsole's output
bytes, not that keypad flag, native scan code, or XKB `<AE02>` position,
unless `.keytab` encodes the distinction as a private event.
The existing `XTGV_BANK_KEYS='+ěščřžýáíé'` already decodes the same UTF-8
characters for Alt+bank selection. Thus the proposed character list can
identify the *likely* main-row numeric positions in this layout without
Shift, but it is a configurable **character alias**, not proof of physical
key identity. A host keymap/private event or native-key frontend is needed
for exact position identity and modifier combinations that collapse to the
same bytes.

## Proposed `[global] numkeys` semantics

```ini
[global]
; Characters are listed in num0, num1, ..., num9 order.
numkeys="é+ěščřžýáí"
```

The proposed quoted value contains exactly ten distinct UTF-8 characters in
`num0` through `num9` order, **not** keyboard-row order. The example maps
`é→num0`, `+→num1`, `ě→num2`, …, `í→num9`. The plain numeral characters
obtained with Shift are not listed because they already arrive as ASCII
digits. `numkeys` is absent/disabled by default on other layouts.

Recommended immediate behavior: `numkeys` is an opt-in shorthand that makes
each matched unmodified character send its ASCII digit by default. Explicit
`[global] numN=...` and `[bankN] numN=...` bindings may replace that default.
Resolution for this alias family is:

1. Explicit binding in the selected bank.
2. Explicit binding in `[global]`.
3. The `numkeys` default `send:<digit>` binding, if configured.
4. Original input bytes when no alias/binding applies.

This makes the single setting useful immediately; it does not require ten
additional `numN=send:N` lines. It also means `numkeys` deliberately changes
plain Czech text input to CP/M. If Petr intended *recognition only* (leaving
the bytes unchanged until ten bindings are added), that is the one semantic
choice to settle before implementation. `none` remains an explicit suppress
binding, not “restore the Czech character”. Restoring an overridden original
character in one bank would need a separate `pass` action; that action should
be specified only where the original bytes are actually available.

The INI loader should validate UTF-8, count ten Unicode scalar values, reject
duplicate entries and control/escape characters, and report a normal INI
error without partially changing the active set. The terminal only needs a
small bounded matcher for these ten input byte sequences, not general Unicode
conversion. Keep quote handling explicit. An ASCII escape form could later
help edit this line from CP/M TE, which is not a UTF-8 editor; the initial
literal example is easiest to edit on the host. Do **not** send this proposed
line to the device INI without Petr's separate approval.

Match only normal keyboard text, before it is forwarded to CP/M. Complete
UTF-8 matches become `numN` events; unmatched or malformed bytes pass through
unchanged. Do not run this alias matcher inside CSI/private command parsing,
XMODEM/RAW mode, or output rendering. Inject the selected ASCII binding
through the ordinary keyboard/history path so local editing sees the digit.
No broad 8-bit filter and no generic CSI filter are part of this change.

## Limits of a character alias (important acceptance criteria)

- A *device-side character matcher alone* cannot distinguish `+` from the
  Czech main row, `+` from the keypad, or pasted `+`: all have the same byte
  value after ordinary Konsole translation. **Konsole can solve the keypad
  case without a native-scancode frontend:** use its documented `+KeyPad` /
  `-KeyPad` distinction in our `.keytab` to tag one path with a private event.
  A keypad `+` rule that merely sends `+` is insufficient—the device alias
  would still catch that byte—so the private event must bypass alias matching.
  Verify this with the real Konsole profile before enabling `numkeys`. Paste
  remains a separate ambiguity: the mirror currently disables bracketed-paste
  reporting, so pasted `+`/`ě` can still trigger a character alias.
- `BS1`/`L3` are dead keys. Their first press may produce **no byte at all**;
  a device-side character matcher cannot detect that press. Repeated press
  produces a character, but that is not equivalent to the original event.
  Those positions require host-side key-event translation for reliable
  per-press overrides while preserving unbound composition.
- Ctrl/Shift/Alt combinations are not inferable in general from the resulting
  bytes. For example, traditional Ctrl+A and Ctrl+Shift+A may coincide, and
  Shift+`numN` produces an ASCII digit shared with the keypad/paste. Keep
  `numkeys` initially to the unmodified Czech output. Modifier-rich overrides
  need the `.keytab`/Kitty private-event transport proposed below.
- Current Alt+Czech-number characters are already consumed by
  `terminal.py` for Alt+bank switching, *before* device INI parsing. New
  `alt_numN` entries would not override that terminal-owned chord unless the
  host map is intentionally changed. Likewise Ctrl+Shift+digits currently
  control backlight; document these reserved chords in the INI reference.
- `console_bits=8` affects RunCPM console byte masking; it is not a UTF-8 to
  CP/M character-set converter. Diagnose the precise CP/M-side no-output
  behavior separately rather than treating 8-bit mode as this fix.

## Full key override extension after the `numkeys` pilot

Keep the original 260919 target vocabulary: existing navigation keys, new
Backspace/Escape/Enter/Space/F1–F12, marked punctuation positions, main-row
`num0`–`num9`, and letters only when Ctrl or Alt is present. Accept all
requested modifier subsets in fixed `ctrl_shift_alt_` order. Keep global and
bank bindings sparse; the current 30-key array/32-bit explicit mask cannot
simply be expanded to every combination. Preserve input preset behavior and
the rule bank explicit > global explicit > preset/default > original input.

For a key that the host can identify as a Qt key+modifier chord, the host
adapter may send a private event to the device, allowing `ctrl_shift_a`, F5,
or a punctuation chord to be bound in the active bank. First prove a pilot
in Konsole and Kitty: F5, Ctrl+Shift+A, a main-row number, one ordinary
punctuation key, and one dead key. Avoid stealing unbound keys: the host must
provide a faithful fallback or intercept only explicitly mapped chords.
Current `.keytab` names are Qt logical keys, not guaranteed physical XKB
positions; a true arbitrary-layout physical-ID guarantee remains a separate
native-input/protocol project. Do not pretend the `numkeys` character alias
solves that larger problem.

Remove the nonstandard F5→`Ctrl+Q F` host mapping and redundant
Alt+Shift+digit backlight bindings in that host-adapter stage, as already
requested. No firmware build or reflash until Petr explicitly says `rebuild` /
`reflash`; never overwrite device `A0:7XTGVTRM.INI` without specific approval.

## Minimal test gates

1. Host/unit tests: exact ten-character parsing, invalid UTF-8/duplicates,
   `é+ěščřžýáí` alias order, bank/global precedence, `none`, untouched bytes,
   incomplete UTF-8, and RAW/CSI bypass.
2. Konsole pilot: unmodified number row sends 0–9 in CP/M with the shorthand;
   Shift still sends 0–9. Add/test a `KeyPad`-conditioned private event so
   keypad `+` stays `+`, while main-row `+` becomes `1`. Separately demonstrate
   the remaining paste ambiguity with pasted `+`/`ě`.
3. Modifier/private-event pilot: distinguish Ctrl+A from Ctrl+Shift+A where
   the host can emit separate events; preserve unbound Czech typing and
   two-press dead-key composition. Test host-owned Alt+bank/backlight chords.
4. Device acceptance in the 7xtgv terminal and its history/editor profiles;
   the native Linux RunCPM port alone cannot test this keyboard path.

Primary references: [Konsole key bindings, including `KeyPad`](https://docs.kde.org/trunk_kf6/en/konsole/konsole/key-bindings.html)
and [Konsole translator API](https://api.kde.org/legacy/4.14-api/applications-apidocs/konsole/html/classKonsole_1_1KeyboardTranslator.html).
