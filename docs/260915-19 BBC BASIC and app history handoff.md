# BBC BASIC and application-history check — 260915

`A1:BBCLOWER.COM` was rebuilt from the untouched `BBCBASIC.COM`. The patch
generator now handles the separate exact `EDITLIST` immediate-command table.
The binary changed by eight bytes compared with the previous `BBCLOWER.COM`:
`EDITLIST` became `editlist`. The A1 upload completed and Petr confirmed that
lowercase `edit 10` works.

The 5-inch terminal runtime could mistake a launcher row such as `A7>mbasic`
for a still-live CCP prompt after MBASIC had started. It now makes the CCP
bank effective only after a freshly rendered `A0>`-style prompt; submitting
that CCP line ends the window. Screen-line import in app banks skips a stale
launcher row. A few leading spaces before a CCP prompt are accepted.

BBC profile #5 now activates history on its editable numbered `EDIT n` row
as well as a `>` prompt. Deleting a recalled BBC item sends the profile's
whole-line clear (`Ctrl-F Ctrl-X`) and cancels any pending retype; the next Up
keeps the history traversal position. This applies to both original
`BBCBASIC` and lowercase `BBCLOWER` because the terminal profile is separate
from the interpreter's keyword case.

Host terminal and runtime tests passed, including synthetic stale-launcher,
numbered-EDIT and deletion cases. A target-correct `-O3` JC8048W550 build is
in `build/7xtgv-vt100-runcpm/jc8048w550/new`; the previous `new` is archived
as `260915-1749 O3 pre-history`, while `current` remains the proven recovery.
At this checkpoint device `REFLASH` resolved to that candidate; its hardware
status was not confirmed in the note. Petr should check filtered insertion inside
`edit 10`, deletion of a recalled BBC line, and whether old `A7>mbasic`
history entries can now be traversed/deleted inside MBASIC. The host test
cannot prove how those native editors echo a burst of controls.

## Follow-up after Petr's BBC `EDIT` test

Petr saw Up replace the whole `EDIT` line during an in-line keyword attempt.
The shared `LineHistory` implementation fell back to whole-line recall when a
nonempty token before the cursor had no matching history entry in bank #5.
It now leaves the line untouched on no match. A successful match still clears
and retypes the whole BBC input line as transport, but the composed result
keeps the prefix and suffix around the completed token. A trained keyword
must be a history entry beginning with that keyword in the *same* bank;
`10 for ...` does not match a query for `f`.

The runtime's bank-5 numbered-EDIT test was replaced with a generic profile
option, `activate_numbered_line=true`; BBC #5 enables it by default and the
source-mirror `7XTGVTRM.INI` states it explicitly. `line_prefix` and
`activate_prefix` now match only at the start of a row after optional display
spaces. A `>` operator in `10 A>B` therefore cannot truncate a source line.
The live device INI was not overwritten because Petr has edited settings
there; the firmware default still enables the BBC option.

Host terminal and runtime tests passed, including no-match preservation,
successful in-line `for` completion retaining `10 ... i = 1 TO 100`, and
source-line `>` handling. A target-correct `-O3` JC8048W550 build is now
`build/7xtgv-vt100-runcpm/jc8048w550/new` (260915-1926). The former
candidate is archived as `260915-1903 live-prompt-history`. This newer
candidate also awaits hardware testing via device `REFLASH`.

The local `cpmdrive` history source mirror contains an empty baseline file;
the current device history was not imported. Completed live history files
have a CRC-32 footer and `.NEW`/`.BAK` generations. For old unwanted entries,
recall + Ctrl-Shift-Backspace + `SYNC` is preferable to manual editing of the
file without regenerating its checksum.

TE/editor in-line completion is a future generic transport case: unlike a
line-oriented REPL, a full-screen editor must receive an insertion at its
current cursor, and a query begun without a whitespace boundary needs an
explicit start gesture or tracked typed-span boundary. No TE transport was
changed in this follow-up.
