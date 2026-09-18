# 260918-23 TP301 input profiles and ANSI attribute marker notes

**Status:** design / implementation note  
**Context:** follows the 2026-09-18 TP 3.01 / TINST testing session on the 7xtgv RunCPM terminal.

## 1. Current INI syntax: unified Exit/Clear vs ordinary key bindings

The current implementation intentionally has two slightly different syntaxes.

Ordinary configurable keys are parsed as **bindings**, so they need an action selector:

```ini
left=send:\e[D
delete=send:\e[3
alt_up=history_previous
down=none
```

For these keys, `send:` means “this binding sends the following decoded byte string”.

By contrast, profile `exit_command` and `clear_command` are already defined as byte strings to send. They go directly through the byte-string decoder and therefore **must not use `send:`**:

```ini
exit_command=\e[Q
clear_command=CLS\r
```

Conceptually:

```text
ordinary key:
    send:\e[D
      -> parseBinding()
      -> decode("\e[D")
      -> ESC [ D

exit_command:
    \e[Q
      -> decode("\e[Q")
      -> ESC [ Q
```

The same escape notation is available to both:

```text
\e       ESC (0x1B)
\r       CR
\n       LF
\t       TAB
\\       literal backslash
\xNN     arbitrary byte
```

Important: literal `\[Q` is **not** a valid current decoder escape. If the intended sequence is the three bytes `ESC [ Q`, write:

```ini
exit_command=\e[Q
```

or equivalently:

```ini
exit_command=\x1b[Q
```

Relevant implementation: `src/7xtgv-vt100-runcpm/history_profiles.cpp`.

---

## 2. TP 3.01 / TINST: private input protocol is legitimate

TINST accepts short byte sequences for editor actions, with the practical four-byte limit observed/documented during testing.

The important architectural point is:

> The keyboard input sequence does not need to be standard ANSI. It is only a contract between the active 7xtgv application profile and the application/TINST configuration.

Therefore it is valid, and often desirable, to shorten modern host sequences for TP.

Current useful TP examples:

```text
physical/logical key       TP/TINST byte sequence
--------------------------------------------------
Left                       ESC [ D
Right                      ESC [ C
Up                         ESC [ A
Down                       ESC [ B
Home                       ESC [ H
End                        ESC [ F

PgUp                       ESC [ 5
PgDn                       ESC [ 6
Delete                     ESC [ 3
```

The last three deliberately omit the normal xterm/ANSI trailing `~`. TINST was observed to accept the three-byte forms while the normal four-byte host sequences were troublesome.

A private three-byte End Edit sequence is also possible:

```text
ESC [ Q
```

with:

```ini
exit_command=\e[Q
```

provided TINST command 38 (End edit) is configured to the same three bytes.

This does **not** imply that `ESC [ Q` is a standard terminal key. It is intentionally a private TP301 input code.

### Keep input and output protocols separate

For TP:

```text
7xtgv -> TP      may use compact/private profile-specific input sequences
TP -> 7xtgv      remains terminal output and should be handled as ANSI/VT
```

Do not weaken the output-side ANSI parser just because the input side uses private codes.

---

## 3. Profiles should normalize old applications to the user's keyboard language

The goal is no longer merely “make TP usable”.

A stronger model is:

```text
physical key
    -> semantic key/action
    -> selected keyboard personality
    -> application profile encoding
    -> bytes accepted by the old application
```

For Petr's normal use, the preferred personality is approximately a **modern keyboard**:

- arrows = cursor movement;
- Home / End = line boundaries;
- PgUp / PgDn = paging;
- Delete = delete at cursor;
- Backspace = delete left;
- Insert = insert/overwrite if the application supports it;
- convenient modern shortcuts should map to application actions;
- historical WordStar control sequences are implementation details, not the desired user interface.

During TP bring-up, several Ctrl-prefixed TINST definitions are useful as temporary “mental workaround” bindings because the current input detector does not yet expose every useful modern key/chord.

The inverse should also be possible. A user who prefers WordStar conventions could select a **WordStar keyboard personality** and make non-WordStar applications behave in familiar WordStar style.

Longer-term it is therefore useful to distinguish:

```text
application profile:
    TP301
    FAND
    TE
    BBC BASIC
    COMAL
    ...

keyboard personality:
    MODERN
    WORDSTAR
    VT100-like
    user/custom
    ...
```

The application profile should answer “what bytes make this application perform semantic action X?”.  
The keyboard personality should answer “what physical key/chord means semantic action X for this user?”.

This avoids cloning whole application profiles just because users prefer different keyboard traditions.

### Future input detector extensions

The current profile grammar covers plain/Ctrl/Alt navigation keys but does not yet expose all useful inputs.

Likely later additions:

- Ctrl + letters;
- Alt + letters;
- function keys;
- possibly selected Shift-modified keys;
- other semantic actions as needed.

These are not required to finish the current TP301 support, but they fit naturally into the abstraction above.

---

## 4. ANSI attributes as 7xtgv semantic marker bits

Current VT100/ANSI-like attributes of interest are:

```text
BRIGHT
UNDERLINE
REVERSE
BLINK
```

The original attributes may be combined, so the renderer must not assume they are mutually exclusive.

### Proposed 7xtgv interpretation

Keep:

- **BRIGHT** as brightness/intensity information;
- **BLINK** as actual blinking/attention information.

Repurpose the two static legacy emphasis bits:

- **UNDERLINE**
- **REVERSE**

as a 2-bit **paper marker selector**.

That gives four paper states:

```text
UNDERLINE  REVERSE     rendered paper
---------------------------------------
0          0           normal/default
1          0           marker 1
0          1           marker 2
1          1           marker 3
```

BRIGHT and BLINK remain independent, so no attribute information needs to be discarded.

This is preferable to a precedence scheme such as “reverse wins over underline”, because the combination remains representable.

### Per-profile mapping

Different applications use underline/reverse for different purposes. The interpretation should therefore be profile-configurable.

Possible future model:

```ini
attribute_mode=markers
underline_marker=1
reverse_marker=2
underline_reverse_marker=3
```

A strict compatibility profile could instead request literal behavior:

```ini
attribute_mode=ansi
```

The exact INI names are provisional; this is an architectural proposal, not yet a frozen format.

A profile may also permute marker meaning if an application uses `reverse` for its primary selection state and that state should receive a preferred marker paper.

---

## 5. Visual themes: PAPER, CRT GREEN, CRT AMBER

Logical terminal attributes should remain theme-independent. The renderer/theme LUT decides how they look.

### PAPER mode

Petr's preferred normal mode:

- white default paper;
- dark/black ink;
- pale RGB marker papers;
- BRIGHT remains an ink/intensity distinction.

Example concept:

```text
marker 0 = white
marker 1 = pale R
marker 2 = pale G
marker 3 = pale B
```

Exact RGB values remain to be chosen.

### CRT GREEN / CRT AMBER modes

The visual concept is reversed:

- black or very dark default paper;
- green or amber phosphor-style ink;
- no normal “black ink” on the dark CRT paper;
- logical black can map to white/bright neutral instead;
- existing silver/middle-grey brightness levels remain useful and should stay distinguishable;
- marker 1..3 become dark/dawn RGB paper variations rather than pale papers.

The same logical cell/attribute buffer can therefore render through:

```text
PAPER
CRT_GREEN
CRT_AMBER
```

without rewriting terminal contents.

---

## 6. Optional CRT scanline effect

A luxury renderer option may add scanline texture by alternating physical horizontal pixel-row intensity.

Possible experiments:

1. modulate background only;
2. modulate foreground and background together;
3. modulate background strongly and glyph pixels only slightly.

At 1x raster scale the effect should be subtle to avoid damaging thin glyph strokes.

At 2x/3x/4x zoom, scanlines can be more convincing because one logical raster row spans multiple physical rows. For example at 2x:

```text
logical raster row
    physical row A = full intensity
    physical row B = slightly reduced intensity
```

No final rendering rule is fixed yet; this should be tested visually on the actual TFTs.

---

## 7. Measure real attribute usage instead of guessing

No convincing corpus statistics were found for how often historical terminal applications actually used combinations such as UNDERLINE+REVERSE or combinations involving BLINK.

For 7xtgv, the better answer is to collect our own statistics from the CP/M software we actually care about.

Add optional/debug SGR attribute telemetry.

Represent the current attribute state as four bits, e.g.:

```text
bit 0 = BRIGHT
bit 1 = UNDERLINE
bit 2 = REVERSE
bit 3 = BLINK
```

Then maintain 16 counters.

Two measurements are useful:

```text
sgr_transitions[16]   how often the parser enters each attribute combination
sgr_cells[16]         how many displayed character writes occur in each combination
```

The cell count is especially important: one SGR transition may style an entire menu/status line.

Collect results per application/profile where practical:

- TP301;
- FAND;
- TE;
- WordStar and similar editors;
- BASICs;
- other full-screen CP/M applications.

This gives real data for choosing marker mappings and tells us whether unusual combinations are worth special treatment.

The telemetry should be diagnostic and cheap to disable/remove in production builds.

---

## 8. Possible profile expansion: 0-9 + a-z

Ten profiles are already becoming restrictive.

A natural future namespace is:

```text
0..9 + a..z = 36 profiles
```

This is attractive because application profiles are evolving from simple history banks into complete compatibility/personality descriptors.

However, the current code assumes ten banks in several places. This is **not** just a cosmetic parser change.

Known current assumptions include:

- `HistoryProfiles::Banks = 10`;
- fixed ten-entry compiled defaults;
- INI parsing currently expects numeric `[bank%d]`;
- history storage/persistence currently has ten 100-line banks;
- selectors/UI/help text assume `#0..#9`;
- bank 9 currently has a special GENERAL/automation role.

Any expansion should therefore be treated as a small format/UI migration and checked for compatibility with existing `7XTGVTRM.BUF` history storage.

A possible future selector notation is:

```text
#0 .. #9
#a .. #z
```

No implementation decision is required immediately.

---

## 9. Near-term implementation priorities

1. Finish the TP301 keyboard profile with short TINST-compatible sequences.
2. Keep modern physical-key semantics as the user-facing goal.
3. Add only the extra detected keys needed by real applications.
4. Keep TP/FAND output handling generic ANSI/VT; fix missing output primitives rather than introducing application-specific screen hacks.
5. Add optional 16-state attribute telemetry before freezing underline/reverse marker mappings.
6. Prototype PAPER / CRT GREEN / CRT AMBER rendering as theme LUTs over the same logical attribute state.
7. Experiment with scanlines visually, especially at zoom >1.
8. Consider 36 profiles later, after separating application profile semantics from history-bank storage assumptions.

## 10. Core design principle

The useful separation is:

```text
INPUT:
physical keyboard
    -> keyboard personality
    -> application profile
    -> private/standard bytes accepted by the program

OUTPUT:
application
    -> ANSI/VT/native 7xtgv attributes and screen operations
    -> logical terminal state
    -> selected PAPER/CRT renderer theme
```

This lets 7xtgv modernize the human interface of old applications without requiring those applications to be modified, while still allowing historical keyboard personalities for users who prefer them.
