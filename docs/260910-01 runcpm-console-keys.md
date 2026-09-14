# RunCPM console keys

This describes RunCPM's buffered line editor (`BDOS` function 10) and the
7xtgv Kitty/Konsole PC-key profile. It applies to the RunCPM command prompt and
to programs using BDOS buffered input. Programs reading the console directly
may define different keys.

`^X` below means `Ctrl+X`.

## RunCPM line-editor controls

| Raw key | Function | 7xtgv PC-key mapping |
|---|---|---|
| `^A` | Move left one character | `Left` in bank profile `#0 CP/M` |
| `^B` | Toggle between beginning and end of line | Direct `Ctrl+B`; physical Home is made stable by 7xtgv with repeated `^A` moves. |
| `^C` | Abort input when the line is empty | No special mapping; press `Ctrl+C` |
| `^E` | Continue at the beginning of the next display line | `Ctrl+Insert` sends `^Q ^E` |
| `^F` | Move right one character | `Right` in `#0 CP/M` |
| `^G` | Delete the character under the cursor | `Delete` in `#0 CP/M` |
| `^H` or `DEL` | Delete the character left of the cursor | `Backspace` sends `^H` |
| `^J` or `^M` | Finish and execute the line | `Enter` sends `^M` |
| `^K` | Delete from the cursor through the end of line | `Ctrl+Delete`; also `Ctrl+End` sends `^Q ^K` |
| `^R` | Redisplay the current line | No special mapping; press `Ctrl+R` |
| `^U` | Delete the entire line | No special mapping; press `Ctrl+U` |
| `^W` | At line start, recall RunCPM's previous command; within a line, move to its end | Direct `Ctrl+W`; physical End is made stable by 7xtgv with repeated `^F` moves. |
| `^X` | Delete everything left of the cursor | `Ctrl+Backspace`; also `Ctrl+Home` sends `^Q ^X` |
| `^?` in RunCPM's help (`0x1F`, formally `^_`) | Display RunCPM's editing-key help | No special mapping; try `Ctrl+?`, `Ctrl+_`, or `Ctrl+Shift+-`, depending on terminal and layout |

Plain navigation keys are logical 7xtgv events. The selected application
profile maps them using `A0:7XTGVTRM.INI`. At the detected CP/M prompt,
7xtgv intercepts physical Home/End to avoid RunCPM's ambiguous `^B`/`^W`
semantics. Some applications use `^Q`
as their own command prefix; the explicitly prefixed variants remain on
`Ctrl+Left` and `Ctrl+Right`.

Controls without dedicated PC-key aliases are `^C`, `^R`, `^U`, and RunCPM's
`^?` help key (`0x1F`).
They remain available through their direct `Ctrl` combinations.

## Other PC-key mappings

| PC key | Bytes or action | RunCPM prompt result |
|---|---|---|
| `PgUp`, `PgDn` | Selected application profile | No default action in `#0 CP/M`; TE uses page movement |
| `Insert` | Selected bank profile | `^V` in `#0 CP/M`; no RunCPM line-editor action |
| `F5` | `^Q F` | Application-oriented; at the prompt it can insert `F` |
| `Up`, `Down` | 7xtgv private controls | Recall previous/next line in the selected 7xtgv history list; at the CCP prompt recall moves to line start with `^A` and deletes with `^G`, so it replaces the whole native line even from a middle cursor. |
| `Alt+0` ... `Alt+9` | 7xtgv private controls | Select named history/key profile `#0` ... `#9` |
| `Alt+Space` | 7xtgv private control | Temporarily show the active bank without changing it or the current line |
| `Alt+#`, or `Ctrl+]` then `#` | 7xtgv private control | Toggle `## RAW`; bypass history and application input translation |
| `Alt+PgUp`, `Alt+PgDown` | 7xtgv private controls | Select previous/next history list |
| Plain/Ctrl/Alt arrows and six-key block | Selected application profile | Configurable in `A0:7XTGVTRM.INI` |
| `Shift+Alt+PgUp`, `Shift+Alt+PgDown` | Selected application profile | Alt-modified application page keys |

`Alt+PgUp/PgDn` remains reserved for profile browsing. The helper's
`Ctrl+] a/e/h/l` commands retain viewport edge and pan controls.

The initial named profiles are CP/M, MBASIC, TE, TURBO, COMAL, BBC BASIC, DDT,
VTESTS, ANSI APP and GENERAL. MBASIC, DDT and VTESTS suppress profile
editing keys until safe application controls are established. TE uses its own
documented WordStar mappings: `^S`/`^D` for one character and `^Q^S`/`^Q^D`
for line beginning/end. ANSI-oriented profiles use standard escape sequences.
Plain Up/Down and Alt+Up/Down are also per-profile: TE uses plain arrows for
editing and Alt+Up/Down for history, while CP/M uses plain arrows for history.

## Mapping files

- Kitty: `src/7xtgv-vt100-runcpm/tools/kitty.conf`
- Konsole: `src/7xtgv-vt100-runcpm/tools/7xtgv-cpm.keytab`
- Launchers: `src/7xtgv-vt100-runcpm/tools/kit.sh` and `kon.sh`

Both launchers attach to the same `7xtgv-cpm` tmux session. This permits
several desktop views while only one `terminal.py` process reads the serial
device. Use `cpm-session.sh --monitor` for a read-only view and
`cpm-session.sh --stop` to release the serial port. Tmux's Ctrl+B prefix is
disabled so the RunCPM HOME mapping reaches CP/M.
