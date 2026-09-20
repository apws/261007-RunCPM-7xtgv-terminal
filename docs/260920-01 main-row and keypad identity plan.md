# Main-row digits and keypad decimal — corrected keyboard plan

Status: **draft for implementation sign-off**. This refines the earlier
character-alias proposal in
[`260920-00 terminal numkeys aliases and key-event plan.md`](260920-00%20terminal%20numkeys%20aliases%20and%20key-event%20plan.md).
The isolated Konsole test proved that this Czech layout's ten main-row keys
can be recognized by the host before character translation. The primary
implementation therefore needs no `numkeys` setting: host key events become
semantic `numN` events, whose default output is the ASCII digit. A configurable
character-alias set remains a possible fallback for other hosts/layouts, not
a requirement for this implementation. No firmware, host keymap, or device
INI is changed by this plan.

## Desired behavior

- `num0` … `num9` name the **main keyboard number-row positions**, not the
  numeric keypad. Konsole identifies the ten Czech main-row keys, including
  the unshifted `+` on the physical 1 key, and sends semantic `numN` events.
  Without an explicit override, each sends its ASCII digit to CP/M, not its
  Czech character. With `[global]` or `[bankN]` bindings, each `numN` may
  send any supported sequence/action. Shifted and other modifier combinations
  remain separately nameable using the agreed `ctrl_shift_alt_` prefix order.
- The numeric keypad keeps its existing numbers and `+ - * /` behavior and
  is **not** swept into `num0` … `num9` bindings. Konsole's documented
  `KeyPad` condition can distinguish keypad from main-row events before it
  emits terminal bytes. Our current PTY byte stream cannot reconstruct that
  distinction after both paths have emitted the same character.
- Add `kp_decimal` for the numeric keypad's physical `.`/Del key with NumLock
  on, the expected normal state. Depending on the host layout it may emit
  comma or dot; the terminal default is **ASCII `.`** for CP/M in either case.
  An explicit `[global]` or `[bankN]` `kp_decimal` binding may override it to
  any supported action/sequence. The same physical
  key in NumLock-off navigation mode is `kp_delete`; it remains ordinary
  Delete by default and may be overridden *separately*. These are standard
  US/XKB-style names (`<KPDL>` has `KP_Decimal`/`KP_Delete` levels).
- Preserve all other unbound keypad keys and their current output. Do not
  globally change the locale's decimal separator outside this terminal.

Example INI intent (not valid in today's firmware yet):

```ini
[global]
; Defaults: main-row num0..num9 send ASCII 0..9; kp_decimal sends ASCII dot.
; Optional: these override the defaults.
; num1=send:!
; kp_decimal=send:,
; kp_delete=send:\x1b[3~
```

## Where the identity comes from

In stock Konsole, a keytab rule can distinguish `+KeyPad` from `-KeyPad`.
Therefore keypad-plus versus main-row-plus does **not** require reading raw
hardware scan codes or accepting a keypad-plus collision. It *does* require
our Konsole keytab to tag the intended event before sending bytes to the
Python mirror. A rule that sends an ordinary `+` for both paths would lose
the distinction again. Use a non-colliding private event, then apply the
active global/bank binding in firmware.

For the Czech main row, the unshifted Qt keys yield different characters
from US ASCII digits. The isolated 260920 Konsole probe below confirmed that
`.keytab` accepts the literal Czech key names. In the current layout, map
those ten distinct host events to semantic `numN` events; no Czech strings
need to be parsed in firmware, and pasted `+`/`ě` remains pasted text. This
host adapter is necessarily layout-aware, while the firmware INI remains
layout-neutral. If a future layout cannot be handled by a host key-event
adapter, an optional configurable `numkeys` character alias could be added
then, with its paste ambiguity stated explicitly. Do not silently treat
keypad `+` as `num1`.

For keypad isolation, Konsole must tag keypad digit and operator events
before PTY translation, not only keypad `+`/decimal. Otherwise raw keypad
`1` and an ASCII main-row `1` look identical to the device, and a `num1`
override would alter both. The tagged keypad path bypasses main-row `numN`
bindings. Host private events still need a faithful
fallback for unbound keys, including NumLock and layout-dependent output.

Similarly pilot the numeric keypad decimal event under both NumLock states:
the host may report numeric mode as `Comma+KeyPad` on this Czech layout and
navigation mode as `Delete+KeyPad`; a US layout may report `Period+KeyPad`.
Normalize those host events to `kp_decimal` and `kp_delete` respectively,
with ASCII dot as the `kp_decimal` default and the existing Delete behavior
as the `kp_delete` default.
These exact Qt event names are to be **measured**, not presumed from the XKB
symbol list alone. The project's current Konsole `Delete` rule has no
`-KeyPad` condition, so it may also catch keypad Delete; the host adapter
must split that rule when adding `kp_delete`.

## Firmware/profile side

Use bounded sparse `[global]` and `[bankN]` bindings; do not expand the
existing 30-key arrays to every key × modifier combination. Resolution is
bank explicit > global explicit > default digit for plain `numN` (or original
keypad behavior for `kp_delete`, ASCII dot for `kp_decimal`) > original host
bytes. Resolve
the tested Czech main-row host events and ordinary main-row ASCII digit events
to the same semantic `numN` identities; leave keypad events on their separate
path. No `numkeys` parser is required for the initial implementation.
`none` remains an explicit suppress action. Host private events need a
fallback representation for any key whose default differs by layout or
NumLock state; an unbound `kp_decimal` explicitly produces ASCII dot.

Actions sending printable digits, dot, or comma must enter through the normal
keyboard/history path so the line editor's model remains synchronized.
Preserve RAW mode, CSI keyboard input, Alt+bank selection, and currently
terminal-owned chords. The full F1–F12/letters/punctuation/modifier matrix
remains in the [260919 plan](260919-19%20terminal%20keyboard%20physical%20keys%20and%20sparse%20overrides%20plan.md);
this main-row/keypad pilot should be the first host-input proof for it.

## Acceptance gates

1. Observe in Konsole which keytab keys/modes fire for main-row `+`, `ě`,
   shifted digits, keypad `+`, keypad comma/period, and keypad Delete with
   NumLock off. **Done for the current X11 map using synthetic physical X11
   keycodes**; verify Petr's physical decimal/comma key separately if its
   reported output differs from this probe.
2. Confirm private events distinguish main row from keypad while normal
   input and unbound keypad output remain unchanged. Confirm all ten Czech
   row keys and the main-row digits under another layout reach the same
   `numN` bindings, while keypad digits/operators and pasted text remain
   independent. Confirm the numeric keypad decimal defaults to ASCII dot
   whether the host originally produces comma or dot, and an explicit
   `kp_decimal` override changes only that key.
3. Test bank/global precedence, history line tracking, RAW mode, and the
   existing Alt+bank and backlight shortcuts in host tests, then on the real
   7xtgv terminal when Petr requests rebuild/reflash.

References: [Konsole key binding `KeyPad` mode](https://docs.kde.org/trunk_kf6/en/konsole/konsole/key-bindings.html);
local XKB `/usr/share/X11/xkb/symbols/keypad` (`<KPDL>`).

## Isolated Konsole result (260920)

A temporary `.keytab` was loaded with `--builtin-profile -p KeyBindings=...`
in a separate Konsole, running a local raw-byte capture instead of RunCPM.
XTest pressed active-X11 physical keycodes `10`–`19`, Shift+`11`, keypad `86`
(`+`), keypad `88` (`2`), and keypad `91` with NumLock in both states. The
probe verified its own window PID/title/focus before each injected key,
restored the previous focus and NumLock state, and did not touch the shared
mirror or device.

The capture was exactly `ROW_1 ROW_2 ... ROW_9 ROW_0`, then `SHIFT_ROW_2`,
`KP_PLUS`, `KP_2`, `KP_PERIOD`, `KP_DELETE` (markers were contiguous; spaces
here separate them for reading). Thus the ten Czech row symbols and keypad
events can be separated *in Konsole before PTY translation* on this machine.
In this synthetic test, the numeric keypad decimal produced `.`; Petr has
reported comma from his physical keyboard, so include both keypad `.` and
`,` cases in the actual host adapter and verify the physical key later.

Konsole's keytab parser matched printable punctuation using literal key
names `+`, `.`, and `,`; the attempted word names `Plus`, `Period`, and
`Comma` did **not** match. Literal Czech `ě š č ř ž ý á í é` names did match.
The `+KeyPad`/`-KeyPad` condition distinguished keypad `+` from main-row `+`,
and `Delete+KeyPad` matched the NumLock-off keypad key. No firmware parser or
INI binding has yet been implemented or tested.
