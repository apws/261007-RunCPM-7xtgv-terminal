# Terminal application profiles

The active `#0`–`#9` selection combines three things:

- one independent 100-line history bank;
- an application name and keyboard mapping;
- an optional byte string emitted after selection.

The firmware reads `A/0/7XTGVTRM.INI` once after mounting the SD card and before
starting the CP/M command processor. The project mirror is
`cpmdrive/A0 - CP-M core/7XTGVTRM.INI`. Compiled defaults are used when the file
is absent; invalid entries are reported on the boot screen.

## Format

Each `[bank0]` through `[bank9]` section accepts:

| Setting | Meaning |
|---|---|
| `name` | Label shown as `#n name` during selection |
| `on_select` | Bytes emitted after the one-second label delay |
| `left`, `right`, `up`, `down` | Plain arrow behavior |
| `home`, `end`, `insert`, `delete`, `page_up`, `page_down` | Plain six-key block |
| `ctrl_` + any name above | Ctrl-modified behavior |
| `alt_` + any name above | Alt-modified behavior |

A key value is `none`, `history_previous`, `history_next`, or `send:` followed
by bytes. Strings recognize `\e`, `\r`, `\n`, `\t`, `\\`, and `\xNN`.

```ini
[bank2]
name=TE
on_select=A0:\r
left=send:\x13
right=send:\x04
home=send:\x11S
end=send:\x11D
page_up=send:\x03
page_down=send:\x12
up=send:\x05
down=send:\x18
alt_up=history_previous
alt_down=history_next
```

Selecting another bank before the delay expires replaces the pending command;
only the final bank's `on_select` runs. The command is unconditional, so change
profiles after returning to the CP/M prompt if `on_select` contains a drive or
program command. Use an empty `on_select=` to disable it for a bank.

Configuration changes take effect at the next device boot. The parser accepts
normal LF or CRLF files and CP/M `Ctrl+Z` end-of-file padding.

`Alt+PgUp/PgDn` remains reserved for fast profile selection. Configured
`alt_page_up/down` bindings use `Shift+Alt+PgUp/PgDn`. Viewport movement remains
available through the Python helper's `Ctrl+] a/e/h/l` commands.

`Alt+Space` temporarily overlays the active `#n name`, then restores the exact
screen row. It does not change banks, discard input, add history, or execute
`on_select`. Bank `#9 GENERAL` is reserved for automated terminal commands;
interactive application histories belong in banks `#0` through `#8`.

`Alt+#` or `Ctrl+]` followed by `#` toggles the temporary `## RAW` input
bypass. RAW preserves the underlying bank and the normal CP/M output/display
path, while ordinary input bypasses history and profile translation. Selecting
any numbered bank leaves RAW. `Alt+Space` reports `[## RAW / #n]` while active.
Host tools use explicit private RAW-on/off controls, avoiding keyboard-layout
ambiguity.
