# 260919-13 7xtgv keymap personalities and control notation

**Status:** design / implementation follow-up  
**Context:** starts from the 2026-09-19 TP301/TINST test where Ctrl-modified six-pack mappings were entered into `7XTGVTRM.INI` and appeared as literal characters instead of TP editor control sequences.

## 1. Immediate TP301 INI correction

The attempted mappings used human caret notation such as:

```ini
ctrl_insert=send:^N
ctrl_delete=send:^Ld
ctrl_home=send:^Fb
ctrl_end=send:^Fe
ctrl_pageup=send:^Pb
ctrl_pagedown=send:^Pe
```

With the current parser, bare `^N`, `^L`, etc. are not interpreted as ASCII control codes. They are ordinary printable characters, so TP sees/inserts the characters `^`, `N`, etc.

The current reliable form is explicit byte notation:

```ini
ctrl_insert=send:\x0e
ctrl_delete=send:\x0cd
ctrl_home=send:\x06b
ctrl_end=send:\x06e
ctrl_page_up=send:\x10b
ctrl_page_down=send:\x10e
```

These decode as:

```text
Ctrl+Insert   -> ^N
Ctrl+Delete   -> ^L d
Ctrl+Home     -> ^F b
Ctrl+End      -> ^F e
Ctrl+PgUp     -> ^P b
Ctrl+PgDn     -> ^P e
```

where:

```text
^N = 0x0E
^L = 0x0C
^F = 0x06
^P = 0x10
```

and the following `b/d/e` are ordinary ASCII bytes.

Also note the exact current INI key names:

```ini
ctrl_page_up=
ctrl_page_down=
```

not `ctrl_pageup` / `ctrl_pagedown`.

The current TP "six-pack" private input forms remain valid:

```ini
insert=send:\e[2
delete=send:\e[3
home=send:\e[H
end=send:\e[F
page_up=send:\e[5
page_down=send:\e[6
```

These deliberately use the short TP/TINST contract rather than standard xterm `~`-terminated forms.

---

## 2. Existing `input=` presets are defaults, not the final user-interface model

The current profile parser already supports:

```ini
input=custom
input=ansi
input=vt52
input=wordstar
```

These are useful preset/default mappings. Explicit INI key bindings override preset-generated bindings.

For TP3, this means TINST can usually keep its historical/default WordStar editor commands rather than redefining every action. The 7xtgv profile can translate modern physical keys into those historical TP bytes.

Important distinction:

> TINST commands should not generally be changed to `Nothing` merely because 7xtgv has a profile.

`Nothing` means TP has no accepted byte sequence for that editor action. If `input=wordstar` or a later compatibility mapping sends the traditional TP/WordStar bytes, leaving TINST at its normal definitions is cleaner.

Only actions where we deliberately want private/additional TP input codes need custom TINST definitions.

---

## 3. The larger goal: normalize many old applications at the terminal side

The profile system should not be thought of only as "TP compatibility".

The real goal is to make diverse old applications controllable through one coherent 7xtgv keyboard convention.

Conceptually:

```text
physical key/chord
      |
      v
user keyboard personality
      |
      v
semantic action
      |
      v
application-specific encoding
      |
      v
bytes accepted by the application
```

Examples of semantic actions:

```text
CURSOR_LEFT
CURSOR_RIGHT
CURSOR_UP
CURSOR_DOWN
LINE_HOME
LINE_END
PAGE_UP
PAGE_DOWN
INSERT_TOGGLE
DELETE_CHAR
DELETE_LEFT
FILE_HOME
FILE_END
FIND
REPLACE
EXIT_EDITOR
...
```

The user should ideally learn one keyboard language while each application profile handles its historical byte protocol underneath.

---

## 4. "mixworx" / "mixworx_wordstar" personality idea

A future preset/personality such as:

```ini
input=mixworx
```

or:

```ini
input=mixworx_wordstar
```

could represent Petr's preferred modernized keyboard semantics.

Possible modern convention:

```text
physical key      semantic action
---------------------------------------------
Arrows            cursor movement
Home / End        beginning/end of line
PgUp / PgDn       page movement
Insert            insert/overwrite toggle
Delete            delete character at cursor
Backspace         delete character left
Ctrl+Home         beginning of file
Ctrl+End          end of file
Ctrl+PgUp         larger/top navigation action
Ctrl+PgDn         larger/bottom navigation action
Alt+Esc           leave current editor/dialog
Ctrl+F            find
...
```

The exact actions remain application-dependent, but the physical convention should be stable.

For a WordStar-native application, 7xtgv might encode those semantic actions as WordStar controls. For TP3 it may send TINST's configured sequences. For FAND, TE, COMAL, SuperCalc, etc., the same physical keys could be translated to entirely different bytes.

The inverse should also be supported: a veteran user may choose a WordStar-like keyboard personality and make even non-WordStar applications behave in a familiar WordStar style.

Thus two concepts should eventually be separable:

```text
application profile
    TP301
    FAND
    TE
    BBC BASIC
    COMAL
    ...

keyboard personality
    MIXWORX / MODERN
    WORDSTAR
    VT100-like
    custom/user
    ...
```

The current `input=ansi/vt52/wordstar` presets are a useful first stage, but they still partially combine "user keyboard convention" and "application byte protocol".

---

## 5. This abstraction is not CP/M-specific

The same terminal-side normalization can be reused outside CP/M.

Potential environments include:

- FUZIX;
- Unix-like shells and curses applications;
- serial-connected retro machines;
- BASIC interpreters;
- monitors/debuggers;
- remote terminal sessions;
- other systems where applications expect inconsistent terminal keyboard conventions.

The useful invariant is:

```text
user-facing keyboard semantics stay stable
application-facing byte sequences vary
```

This makes the profile/keymap system a general terminal-side human-interface compatibility layer rather than a CP/M-only hack.

---

## 6. Proposed readable control-character notation: `\^X`

The current byte decoder supports:

```text
\e
\r
\n
\t
\\
\xNN
```

It does **not** currently support caret notation.

Because application profiles increasingly contain many historical ASCII control bytes, `\xNN` is technically correct but hard to read. Add an explicit escaped caret form:

```text
\^X
```

This should mean "the ASCII control character conventionally written as Ctrl-X".

Examples:

```ini
left=send:\^S
right=send:\^D
up=send:\^E
down=send:\^X

ctrl_insert=send:\^N
ctrl_delete=send:\^Ld
ctrl_home=send:\^Fb
ctrl_end=send:\^Fe
ctrl_page_up=send:\^Pb
ctrl_page_down=send:\^Pe
```

Expected decoding:

```text
\^N     -> 0E
\^Ld    -> 0C 64
\^Fb    -> 06 62
\^Fe    -> 06 65
\^Pb    -> 10 62
\^Pe    -> 10 65
```

### Why escaped caret, not bare caret

Do **not** reinterpret bare text such as:

```text
^N
```

because users may legitimately want to send the printable characters `^` and `N`.

The backslash makes the control-byte interpretation explicit and keeps the format backward-compatible:

```text
^N      -> printable '^' 'N'
\^N     -> byte 0x0E
```

---

## 7. Recommended complete ASCII caret convention

For consistency, `\^` should implement the traditional ASCII control notation:

```text
\^@ = 00
\^A = 01
...
\^Z = 1A
\^[ = 1B   ESC
\^\ = 1C
\^] = 1D
\^^ = 1E
\^_ = 1F
\^? = 7F   DEL
```

Lowercase input may optionally be accepted as an alias:

```text
\^a == \^A
```

but documentation should prefer uppercase.

Invalid forms such as `\^5` should fail INI parsing rather than silently sending an unexpected byte.

For ESC, keep `\e` as the preferred readable notation even if `\^[` is technically supported:

```ini
exit_command=\e[Q
```

is clearer than using caret notation for terminal escape sequences.

---

## 8. Suggested decoder implementation

The change belongs in the common `decode()` function in:

```text
src/7xtgv-vt100-runcpm/history_profiles.cpp
```

Conceptually:

```cpp
else if (escape == '^') {
    char c = *source++;
    if (!c) return false;

    if (c == '?') {
        value = 0x7f;
    } else {
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';

        if (c < '@' || c > '_')
            return false;

        value = uint8_t(c & 0x1f);
    }
}
```

This is intentionally only a proposal until implemented and tested.

Because `decode()` is shared, the notation should then work consistently in:

- ordinary `send:` bindings;
- `send_cancel:` bindings;
- `exit_command`;
- `clear_command`;
- `on_select`;
- other profile byte-string fields that use the same decoder.

`\xNN` must remain supported as the universal arbitrary-byte escape.

---

## 9. Design principle

Prefer human-readable profile files where possible.

Use:

```ini
left=send:\^S
exit_command=\e[Q
```

instead of:

```ini
left=send:\x13
exit_command=\x1b[Q
```

when both mean the same thing.

Reserve `\xNN` for bytes without a more readable conventional spelling.

This matters increasingly as `7XTGVTRM.INI` becomes a library of application compatibility descriptions that humans will inspect, tune, share, and maintain.

## 10. Near-term follow-up

1. Keep current TP301 mappings functional using `\xNN`.
2. Add and test `\^X` support in the shared decoder.
3. Convert selected profile defaults/docs from opaque hex to readable control notation where useful.
4. Continue testing TP3 with mostly historical TINST mappings plus 7xtgv-side modern physical-key translation.
5. Observe repeated mappings across TP/FAND/TE/etc.; when repetition becomes clear, formalize a reusable `mixworx` / modern keyboard personality instead of duplicating mappings per app.
6. Keep the application/output terminal mode independent from the user input personality so the same keyboard convention can work with ANSI, VT52, FUZIX, and other terminal environments.
