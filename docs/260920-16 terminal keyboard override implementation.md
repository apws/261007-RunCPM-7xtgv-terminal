# Terminal keyboard override implementation — 260920

> Syntax update (260922): ordinary INI chord names now use the sole canonical
> key-first grammar `<key>[_ctrl][_shift][_alt]`. The modifier-first examples
> below describe the original implementation and are retained as historical
> context. See the [signed key-first refactoring plan](260922-13%20key-first%20INI%20chord%20names%20signed%20implementation%20plan.md).

Implements the signed scope in `260920-15 terminal keyboard override signed
implementation scope.md`. The jc8048w550 candidate was rebuilt and reflashed
on 260920. Interactive keyboard acceptance is still pending. No device INI or
history file was changed.

## Configuration and behavior

- A key can be bound in `[global]` or `[bankN]` using the existing binding
  values: `send:`, `send_cancel:`, `history_previous`, `history_next`, `none`.
  Bank explicit beats global explicit; global explicit beats the input preset
  or the new default. A duplicate or overflow is counted as an INI error;
  the first accepted value is retained. The sparse pool holds 128 overrides
  per active configuration, not 128 per bank.
- Names have fixed modifier order: `ctrl_shift_alt_f5`, `shift_num1`,
  `alt_m1`, etc. Navigation, Backspace, Escape, Enter, Space, F1–F12, main
  number row `num0`–`num9`, photographed position names `bs1`, `bs2`, `p1`,
  `p2`, `l1`, `l2`, `l3`, `z1`, `m1`–`m3`, `kp_decimal`, `kp_delete`, and modified
  `a`–`z` are recognized. Plain and Shift-only letters are deliberately not
  bindable. Plain/Shift-only `bs1` and `l3` remain with the host dead-key
  compositor.
- The host keymap emits input-only `CSI >70;1;<base>;<modifiers>;<fallback>v`.
  The fallback consists of three decimal digits per original byte. This
  bounds the event to 61 bytes and avoids CSI final-character collisions.
  A malformed or unknown event passes through unchanged. RAW mode unwraps a
  valid event to its original host bytes, bypassing profile bindings.
- Plain Czech main-row keys become ASCII digits, regardless of the ordinary
  Czech character. NumLock-on keypad decimal becomes ASCII dot. NumLock-off
  keypad Delete retains the selected profile's Delete action. Ordinary
  Backspace retains MBASIC/local-compose handling. Pasted text is not changed.
- The generated host sections in `tools/7xtgv-cpm.keytab`, `kitty.conf`, and
  `kitty-cpm.conf` come from `tools/generate_key_events.py`. Edit the generator,
  then regenerate; do not maintain the three sections separately. Existing
  terminal-owned shortcuts remain outside those generated sections. Plain F5
  no longer emits the old Ctrl+Q,F action.

## Host boundary and limits

The Konsole pilot was tested in an isolated instance, not in the shared
device mirror. It distinguished Czech main-row keys from keypad keys and
delivered the decimal/delete variants and F5. A pilot Ctrl+Alt+A rule also
delivered an event, but was removed because Ctrl+Alt can represent AltGr on
national layouts and its unbound fallback could change symbol input. Ctrl+Shift+A
produced no PTY bytes on this desktop: a host shortcut can consume a chord
before the Konsole keytab, so no firmware mapping can recover it. The Kitty
files pass Kitty's local configuration parser but await physical key tests.

The INI parser accepts all modifier combinations for the vocabulary above.
The host adapters only intercept combinations with known, safe fallback
bytes. They intentionally leave Alt+number bank controls, backlight/zoom
controls, AltGr-sensitive text combinations, and untested punctuation
combinations alone. `bs1` and `l3` remain un-intercepted when plain or only
Shifted; `z1` and several shifted punctuation positions need a separate
host-syntax/fallback probe before they can be safely intercepted. This is
partial physical-key coverage, not a claim that every accepted INI name is
currently reachable from every host keyboard and layout.

The isolated Konsole synthetic keypad-decimal press produced `.`, while
Petr's physical key produced `,`; the keytab handles both, but device
acceptance must include the physical press. The Konsole probe opens a
visible/focused temporary window and can disturb other desktop work. Do not
repeat it casually; prefer parser and non-GUI tests until a specific event
needs validation.

## Local verification

- Sanitized C++ terminal/keyboard/profile test: passed.
- Sanitized C++ runtime/render/mirror test: passed, including bank/global
  digit precedence, keypad decimal and Delete, and RAW fallback.
- Kitty parsed both generated configuration files with zero errors.
- `git diff --check`: passed.
- Target-aware jc8048w550 firmware build: passed; staged in `new` with boot
  stamp `260920-1628` (18% program storage, 56% dynamic memory).
- Real-device boot: upload hashes verified; mirror returned to `A0>` with
  build stamp `260920-1628`; SD, terminal profile, and history loaded OK.
- Interactive keyboard test: pending Petr's acceptance checks.

Device acceptance checklist: rebuild, then reflash; verify Czech main-row
digits and Shift digits, physical keypad comma-to-dot, keypad Delete under
WordStar/VT52 input presets, bank versus global overrides, unbound punctuation
and dead accents, TE/TP3 and MBASIC editor behavior, and RAW transfers.
