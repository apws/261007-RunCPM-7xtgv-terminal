# 260910 — 7xtgv CP/M smart-terminal input/app-profile plan

Status: engineering summary for Codex after discussion/testing of RunCPM + 7xtgv terminal on ESP32-S3.

## 0. Immediate intent

We need the 7xtgv terminal to make old CP/M applications comfortable from a modern keyboard **without pretending that CP/M ever had one universal editing-key standard**.

The present 10 application banks (`#0..#9`) remain the right basic model. The important refinement is to separate:

- terminal/display emulation;
- physical-key -> application-input translation;
- optional application-specific input adapters/state machines;
- local 7xtgv history;
- input source (human keyboard vs host/Codex/internal injection);
- a temporary true RAW/bypass overlay (`##`).

Do not make all of this one giant global key map.

---

## 1. Historical model: CP/M applications fall into several useful classes

### 1.1 CP/M/CCP or software using BDOS buffered line input

CP/M itself had primitive console line editing (not a modern universal cursor-key model). RunCPM's CCP editor is more capable and the existing `#0 CP/M` mappings can therefore be useful, but those mappings are **RunCPM/CCP behavior, not a CP/M-wide application convention**.

Current `#0 CP/M` behavior such as `^A`, `^F`, `^B`, `^W`, etc. must not be assumed safe in other programs.

### 1.2 Application-owned character-at-a-time line editors

Many programs read console characters themselves and define their own semantics. They do not use the CP/M buffered editor.

Two important examples already on our system are radically different:

- **BBC BASIC (Z80):** ordinary full cursor-oriented line editing, but with configurable control codes; the exact codes must be read from the `DIST.Z80` shipped with the binary.
- **Microsoft BASIC-80 / MBASIC 5.30:** old modal command-oriented line editor; modern cursor keys cannot be implemented safely by naive one-key -> one-byte mapping.

### 1.3 Full-screen terminal applications

Full-screen applications are completely normal CP/M software, not some non-CP/M exception. CP/M did not provide a standardized screen/window API, so programs such as WordStar and Turbo Pascal were installed/configured for a particular terminal and emitted that terminal's cursor/erase/attribute sequences themselves.

Their keyboard command conventions were also application-specific, although WordStar-style control keys became a strong de-facto convention.

### 1.4 Machine-specific direct-video/direct-keyboard software

Some games/demos/special software can bypass the generic console and directly access machine-specific video RAM or I/O. That is genuinely outside the generic portable-terminal path and should be treated as a special machine-emulation case, not something the normal 7xtgv CP/M terminal must solve.

---

## 2. WordStar-ish convention: useful de-facto family, not an OS standard

The familiar WordStar diamond is representative:

```text
        ^E
         up
   ^S <-    -> ^D
        down
        ^X

^A / ^F are commonly word-left / word-right in WordStar/Turbo-style editors.
```

Do not globally hard-code this for all CP/M applications. It is, however, an excellent static-map profile family.

Turbo Pascal explicitly documents WordStar-compatible secondary editor commands and allows primary commands to be installed for the actual terminal/keyboard. Its installer permits a physical key or input sequence up to four characters for an editing function. This is historically very close to what 7xtgv app banks should provide.

---

## 3. BBC BASIC (Z80): correct the current profile ASAP

BBC BASIC is **not MBASIC-like**. The CP/M edition has a real character-at-a-time line editor and explicitly says that keyboard codes varied between CP/M machines. Its patch area (`DIST.Z80`) stores the expected editing codes. The particular 5.00 package installed here does not use the older ADM-3A-style table initially assumed in this plan.

Documented supplied defaults include:

| Modern semantic action | BBC BASIC default input |
|---|---|
| Up | `Ctrl-G` |
| Down | `Ctrl-O` |
| Left | `Ctrl-J` |
| Right | `Ctrl-L` |
| Start/Home | `Ctrl-F` |
| End | `Ctrl-N` |
| Backspace + delete-left | `Ctrl-H` |
| Delete at cursor | `Ctrl-R` |
| Clear left of cursor | `Ctrl-U` |
| Clear right of cursor | `Ctrl-X` |
| Insert/overwrite toggle | `Ctrl-Q` |
| Accept line | `Enter` |

These controls are part of BBC BASIC's CP/M line editing, including normal immediate-entry/editing use. Therefore if BBC BASIC currently behaves strangely, first assume **our current app-bank config may be wrong** before inventing an adapter.

Hardware verification on the JC8048W535 ESP32-S3 target confirmed that
`Ctrl-F Ctrl-X` clears the whole active BBC BASIC line. Both controls work in
one burst. The replacement text must follow after a short pause: sending the
clear and new text together produced remnants such as `55PRINT 666`, while a
50 ms configured retype delay allows the BBC editor to settle. Recall requests
are also serialized and rapid Up/Down input is coalesced while BBC echoes the
preceding replacement.

`EDIT n` renders its editable source with three leading presentation spaces.
The generic screen-line FSM therefore has a per-profile `trim_left` option;
BBC enables it so the stored history contains `10 PRINT ...`, not the visual
indentation. Other profiles preserve leading spaces unless configured.

### BBC BASIC implementation action

Create/fix its bank as a simple static input map first:

```text
Left   -> ^J
Right  -> ^L
Up     -> ^G
Down   -> ^O
Home   -> ^F
End    -> ^N
Delete -> ^R
Insert -> ^Q
BS     -> ^H
```

Then test at the plain BASIC prompt with an unsubmitted line. No MBASIC-style edit-mode entry should be needed.

The installed CP/M 5.00 binary requires uppercase BBC keywords. Do not enable a
blind lower-to-upper input conversion: BBC variable names remain case-sensitive
and string contents must preserve case. If this becomes burdensome, add an
explicitly enabled BBC-only lexical adapter which converts only complete keyword
words outside quoted strings and `REM` text; keep a raw/unaltered input option.

Keep local 7xtgv history available through its configured modified keys (e.g. `Alt+Up/Down`) rather than stealing ordinary Up/Down from the application.

---

## 4. MBASIC 5.30: why it is genuinely weird

The installed program is Microsoft BASIC-80 Rev. 5.30. It reads console characters itself and does not use RunCPM's BDOS buffered line editor.

Confirmed locally already:

- `Ctrl-A` while typing an unsubmitted line produces the `! ` prompt and enters BASIC-80 Edit Mode.
- Therefore mapping a modern cursor-left key to `Ctrl-A` is wrong: it changes MBASIC state.
- ordinary Backspace / `Ctrl-H` removes the previous character during normal entry on our target;
- the current conservative MBASIC bank leaves cursor/navigation keys unbound because blindly sending editor commands changes normal-input semantics.

### 4.1 Historical explanation: MBASIC was not a VT100-style visual line editor

This answers the question "how did people use this with ordinary terminals with cursor/Home/End/Insert/Delete keys?"

**Usually they did not get modern dedicated-key editing behavior from MBASIC.** BASIC-80's editor predates the expectation that every interactive program understands a terminal's arrow/Home/End/Insert/Delete sequences.

The BASIC-80 editor is deliberately command-oriented and even works in a hardcopy/printing-terminal style:

- `Space` moves logically right and BASIC prints the characters passed over;
- `Rubout` moves logically left/backspaces and BASIC prints characters as it moves;
- editing is performed through single-letter subcommands rather than terminal cursor-addressing commands.

So MBASIC itself is not saying "move the physical terminal cursor left using VT100 CSI D". It owns a logical line and reprints/backspaces while processing edit commands. A fancy terminal could have dedicated keys or programmable function keys, but unless those keys happened to emit the exact BASIC edit command expected at the exact state, MBASIC did not magically use them as modern Home/End/Ins/Del.

This is why `picocom` does not automatically make MBASIC modern: changing the display terminal to ANSI/VT100 does not change MBASIC's input editor semantics.

The 7xtgv device has an especially useful separation here: its TFT output terminal can stay VT100/ANSI-compatible while a completely independent modern keyboard is translated into whatever old application input model is required.

### 4.2 Exact BASIC-80 Edit Mode facts relevant to an adapter

From the BASIC-80 Release 5.0 editor (compatible with our Rev. 5.30 behavior):

- `Ctrl-A` on the line currently being typed enters Edit Mode and places the edit position at the first character.
- `EDIT <line>` enters Edit Mode for a stored program line; `EDIT .` refers to the current program line.
- a syntax error during execution can automatically enter Edit Mode.

Important Edit Mode commands:

| Command | BASIC-80 meaning |
|---|---|
| `Space` / `[n] Space` | move right, optionally `n` characters |
| `Rubout` / `[n] Rubout` | move left/backspace, optionally `n` characters |
| `I` | enter insertion at current position; `Esc` ends insertion |
| `X` | move to end and enter insertion |
| `D` / `[n]D` | delete characters to the right/current edit position |
| `H` | delete rest of line and enter insertion |
| `S<char>` | search forward |
| `K<char>` | search forward while deleting passed text |
| `C...` | change/replace following character(s) |
| `Enter` | save changes and leave Edit Mode |
| `E` | save/leave without printing remainder |
| `Q` | abandon edits and return to command level |
| `L` | keep edits so far, print/list remainder, reposition to beginning, remain in Edit Mode |
| `A` | restore original line and restart from beginning — **not safe as modern Home**, because it discards edits |

Important consequence: a plausible modern **Home while already editing is `L`, not `A`**. A plausible modern **End is `X` followed immediately by `Esc`** if we only want to position at end without leaving MBASIC in insertion state.

### 4.3 Do not implement MBASIC as a static mapping

Examples of why static mapping is unsafe:

```text
Right -> Space
```

would type an actual space in normal command entry.

```text
End -> X
```

would type `X` normally, but in Edit Mode it means move-to-end + insert.

```text
Home -> Ctrl-A
```

would enter Edit Mode from normal input, but Ctrl-A is not simply "Home" once already in Edit Mode.

MBASIC therefore needs an explicitly enabled adapter/FSM.

---

## 5. MBASIC adapter: make the interface generic, but keep MBASIC logic app-specific

Do **not** build a giant universal CP/M inference engine now. Instead add a small generic adapter interface that can host MBASIC today and another stateful application later if one really appears.

Conceptual interface:

```text
adapter.on_semantic_key(KEY_LEFT / KEY_RIGHT / HOME / END / INS / DEL / BS / ...)
adapter.on_input_byte(byte, source)
adapter.on_guest_output(byte)
adapter.on_reset_or_bank_change()
```

Possible adapter states:

```text
PASS/NORMAL
MB_EDIT_NAV
MB_EDIT_INSERT
UNKNOWN/RESYNC (optional later)
```

### Is input keystroke stream alone sufficient?

**Not completely.** It is enough for the safe first implementation when the adapter itself initiates Edit Mode, but MBASIC can also change state for reasons not represented by a modern-navigation key:

- user can manually type `Ctrl-A`;
- `EDIT n` / `EDIT .` enters Edit Mode after a submitted command;
- a syntax error can automatically enter Edit Mode.

Therefore a reusable robust adapter should be *primarily input-driven* but have access to the **guest output stream for state confirmation/resynchronization**. Do not make output pattern recognition mandatory for the first version.

### Recommended staged implementation

#### Stage A — safe and deterministic current-line enhancement

Only modernize a currently typed MBASIC command/program line when **the adapter itself initiates the transition**.

While normal input is being typed, maintain a small shadow of the current line (printable chars + Backspace handling). The cursor is normally at the end.

On first modern navigation key:

1. send `Ctrl-A` to MBASIC;
2. MBASIC enters Edit Mode at beginning (`! ` observed as confirmation if desired);
3. use the tracked line length to position logically where the modern cursor should be;
4. enter `MB_EDIT_NAV` state.

For positioning from the beginning, BASIC supports an integer prefix for movement, so `N` + `Space` can move right `N` characters. Start conservatively; repeated spaces are simpler for initial validation, numeric-prefix positioning can optimize later.

Approximate modern semantic mapping once in navigation state:

```text
Left    -> Rubout
Right   -> Space
Home    -> L
End     -> X, Esc
Delete  -> D
Enter   -> CR (save/exit)
```

Printable typing at an interior position cannot simply be passed, because letters are edit subcommands there. It should begin insertion:

```text
printable while MB_EDIT_NAV -> I, printable... -> MB_EDIT_INSERT
```

When a navigation key arrives during `MB_EDIT_INSERT`:

```text
send Esc
return to MB_EDIT_NAV
perform requested navigation command
```

Modern Backspace semantics need direct validation. In navigation state, `Rubout` only moves left; likely modern "delete previous character" requires a compound sequence (candidate: move left then `D`). In insertion state BASIC documents Rubout/Delete/Underscore as deleting to the left, but exact behavior with original vs newly inserted text should be tested before hard-coding.

#### Stage B — recognize externally entered MBASIC Edit Mode

Later, optionally recognize/confirm:

- adapter-sent `Ctrl-A` followed by MBASIC's `! ` response;
- `EDIT n` / `EDIT .` transitions;
- automatic syntax-error Edit Mode.

Because arbitrary BASIC programs can print similar text, guest-output recognition must be contextual and per-bank, not a global terminal parser assumption.

A manual/automatic adapter reset on bank switch, BREAK/reset, or obvious BASIC prompt return is desirable.

### Alternative considered: fully local modern line editor

7xtgv could theoretically own the whole current line locally and only send the completed result to MBASIC. That would be very easy to make modern, but it changes timing/echo semantics and can break `INPUT`, `INPUT$`, `INKEY$`, programs expecting immediate characters, etc. Do **not** make this the default MBASIC solution.

The stateful compatibility adapter is more faithful.

---

## 6. History is orthogonal to application editing

The 7xtgv history feature is useful even inside programming editors: e.g. recall a similar command/source line, then edit it. It must therefore **not** be globally disabled just because an application has a full-screen editor.

Current modified history keys such as `Alt+Up/Down` are good because they do not steal the application's ordinary Up/Down cursor movement.

Treat history as its own per-bank policy/service:

```text
physical key
   |
   +-- terminal-local command/history?   (e.g. Alt+Up/Down)
   |
   +-- app mapping / adapter
   |
   +-- CP/M guest
```

Preserve the existing configurable behavior rather than redesigning it unnecessarily.

However, synthetic host/Codex traffic should normally **not enter or erase human history**.

---

## 7. Input-source tagging: prevent Codex/test traffic from contaminating human state

The recent problem was that Codex used `vtput/vtget` while the user's current application bank/history was active and disturbed the current history.

For now Petr instructed Codex to use `#9` for tests. Keep that rule.

Architecturally, input should also carry a source tag if practical:

```text
HUMAN     USB/physical keyboard
HOST      vtput / Codex / external test injection
INTERNAL  terminal-generated control/test bytes
```

Default policy:

```text
HUMAN -> may participate in configured history
HOST  -> does NOT affect human history unless a test explicitly asks for it
INTERNAL -> does NOT affect history
```

This prevents a class of accidental corruption regardless of selected bank.

---

## 8. `#9` and `##`: two different test concepts

### `#9` — persistent TEST/CODEX bank

Reserve/use `#9` as the normal controlled testing profile.

Codex should use `#9` instead of reconfiguring or exercising whichever application bank Petr is currently using, except when explicitly testing bank switching/profile behavior.

Suggested characteristics:

```text
#9 TEST/CODEX
history      = off (or at least host ignored)
adapter      = none
special maps = minimal/deterministic
```

### `##` — temporary RAW/bypass overlay

`##` is **not bank 10/11**. It is a temporary overlay over the selected `#0..#9` bank.

Semantics:

```text
#3 BBCBASIC
   -> ##
[RAW input overlay; underlying bank #3 remembered]
   -> ## again
#3 BBCBASIC restored
```

Selecting `#0..#9` while RAW is active may simply terminate RAW and select that bank.

While `##` is active:

- bypass app-bank input mappings;
- bypass app-specific input adapters/FSMs;
- bypass local history capture/editing/interception;
- do not transform injected `vtput` bytes;
- keep the normal CP/M console **output/display terminal emulation alive**.

This is an **input bypass**, not "turn the TFT terminal off".

This is important for diagnosis: picocom was not truly raw if its received keyboard bytes still passed through the embedded second-core terminal input transformations. `##` gives us a real reference path while retaining the same TFT output parser/rendering.

### Performance

`##` should be the lowest-overhead input path and may noticeably improve `vtput/vtget` transfers because history/mapping/adapter work is skipped. Do not promise a large gain until measured: RunCPM/serial/copy/output-rendering overhead may dominate.

Benchmark `#9` normal versus `#9 + ##` with the same transfer.

---

## 9. Local mode/bank UI

Keep the interaction lightweight; no permanent status bar is required.

Existing/planned terminal-local controls discussed:

```text
Alt+PgUp / Alt+PgDn  previous/next bank
Alt+n                 select bank #n (existing convention as implemented)
#+Space               indicator only: briefly show current bank/mode
Alt+Space             can also blink the current bank/mode
##                    enter/leave temporary RAW overlay
```

Suggested transient display:

```text
[#3 BBCBASIC]
```

and in RAW:

```text
[## RAW / #3]
```

The underlying bank should remain visible in the RAW indicator so the user knows what will be restored.

Make these terminal-local commands; they must not leak as guest characters.

For Codex/machine control, prefer an unambiguous terminal-control operation (`raw on/off`, bank select, etc.) if such a control channel exists, rather than forcing automated tests to simulate local human key chords.

---

## 10. Turbo Pascal 3.01: support/test ASAP as the representative complete IDE

TP 3.x is an excellent near-term integration test because it combines:

- a full-screen editor;
- terminal cursor/erase/attribute handling;
- installed terminal definitions;
- WordStar-compatible editor commands;
- compile/run/menu workflow;
- a substantial real CP/M programming environment rather than just a command prompt.

The Turbo Pascal manual states that its editor's secondary commands are WordStar-compatible, while primary commands can be installed to suit a terminal/keyboard; older manuals allow input command sequences up to four characters. ANSI/VT100 terminal definitions were commonly usable in CP/M Turbo installations.

### Minimal TP301 test plan

Do this as soon as the newly unpacked TP301 is available.

1. Launch TP301 in `#9`/controlled conditions first and identify its current installed terminal definition.
2. Verify the full-screen display: cursor addressing, clear screen/line, attributes, scrolling, menu/status/editor regions.
3. Before changing anything, verify the default WordStar secondary controls (`^S/^D/^E/^X` etc.).
4. Create a dedicated TP bank using static key mapping where possible.
5. Verify modern arrows through that mapping, then editor insert/delete/home/end behaviors as supported by Turbo's command set.
6. Load/edit a `.PAS` file, compile, inspect errors, return to editor, run a trivial program.
7. Verify `Alt+Up/Down` history/local terminal commands do not interfere with ordinary TP navigation.
8. Compare normal bank with `##` raw for exact input when diagnosing anomalies.

Keep automated testing compact. Once a larger implementation block is complete, ask Petr for interactive tests that require the real keyboard/TFT or subjective visual confirmation.

Do not spend time building a huge test suite before the basic TP IDE works.

---

## 11. Output terminal vs keyboard protocol: keep them independent

PC-side terminal programs commonly present ANSI/VT100 output/input conventions today, but the embedded 7xtgv TFT terminal is its own native terminal implementation.

Architecturally this is an advantage:

```text
                     7xtgv
              +----------------+
modern USB -> | keyboard/input | -> app-specific CP/M bytes
keyboard      | translation    |
              |                |
CP/M output ->| terminal       | -> TFT char/attr renderer
              | emulation      |
              +----------------+
```

Do not assume that because the **output** terminal speaks VT100/ANSI, the **application's input editor** wants VT100 cursor-key escape sequences. BBC BASIC wants its configured ADM-3A-ish control bytes by default; MBASIC wants its own modal commands; Turbo can be configured; another program may differ again.

This independence is the core reason the app-bank architecture is valuable.

---

## 12. Future smart-terminal feature: per-app syntax coloring/render intelligence

This deserves preserving in the architecture now, even if implementation comes after TP301/MBASIC stabilization.

7xtgv already owns a native screen model/renderer (character + attribute-oriented design). Therefore syntax coloring does **not necessarily require modifying the old CP/M editor/application at all**.

A per-bank smart-render layer can recolor the terminal's cells after interpreting guest output while leaving the guest's byte stream untouched.

Potential progression:

### Level 1 — simple lexical coloring

Per app bank, enable a tiny native lexer/highlighter:

```text
highlight = none | basic | pascal | asm | ...
```

Apply color only to recognized editable/source text cells. The guest still believes it is writing ordinary monochrome/terminal text.

This could give syntax coloring even to old TE or another plain CP/M text editor.

### Level 2 — application-aware screen regions

Full-screen apps such as Turbo Pascal contain menus/status/help plus an editor viewport. A TP bank could identify the editor region/state and syntax-color only that area, leaving the IDE chrome untouched.

This can begin with deliberately narrow per-app rules rather than trying to infer every possible TUI generically.

### Level 3 — richer smart-terminal overlays

Longer-term 7xtgv ideas can include native, non-guest-destructive overlays such as:

- selection/highlight;
- zoomed region/window;
- global overview/minimap-like view;
- semantic coloring;
- window/panel emphasis;
- terminal-side pseudo-graphical/TUI augmentation.

These are potentially very useful but also deliberately disruptive: the terminal may not inherently know whether it is showing a text editor, programming editor, debugger, spreadsheet, or an entire pseudo-graphical desktop. Therefore all such behavior must be **explicitly per-bank / opt-in and easy to disable or bypass with `##`**.

### Important design rule

Syntax coloring/render intelligence should modify **only 7xtgv display attributes/rendering**, not inject color escape sequences into the CP/M application and not alter the guest's files/data.

This makes it reversible, compatible with old applications, and especially natural for the existing 7xtgv char/attribute buffer architecture.

TP301 should eventually become the first serious test of this idea because it gives a complete IDE whose editor contents and UI chrome coexist on one terminal screen.

---

## 13. Proposed architecture summary

Keep the implementation layered:

```text
INPUT SOURCES
  USB keyboard / UART-picocom / HOST-vtput / INTERNAL
          |
          v
   source tagging
          |
          +--> terminal-local controls
          |      bank switch / indicator / ## / history chord
          |
          v
      ## RAW ? -------------------------------> CP/M console input
          |
          no
          v
      selected #0..#9 bank
          |
          +--> static semantic-key mapping
          |
          +--> optional app adapter/FSM (MBASIC etc.)
          |
          v
      CP/M / RunCPM

CP/M console output
          |
          v
 terminal protocol parser / screen model
          |
          +--> optional per-bank smart render/highlighter
          |
          v
       TFT renderer
```

History should remain a separate terminal-local service and should observe only the sources/configurations intended to participate in it.

---

## 14. Priority order for Codex

### P0 — stabilize diagnostic/test path

- preserve `#9` as TEST/CODEX bank;
- implement `##` RAW overlay;
- implement/finish temporary current-bank/mode blip (`#+Space`, `Alt+Space` as intended);
- ensure RAW preserves/restores underlying bank;
- make HOST/Codex traffic not damage human history, preferably by source tagging; at minimum enforce `#9` + history-off for tests;
- compare `vtput/vtget` speed normal vs RAW.

### P0 — fix BBC BASIC bank

- configure documented BBC BASIC CP/M default editing controls;
- validate arrows/Home/End/Delete/BS on an unsubmitted line;
- no state machine unless evidence proves this particular BBC BASIC binary was patched differently.

### P0/P1 — TP301 complete IDE support

- inspect current installed terminal definition;
- make screen rendering and WordStar-style input work;
- perform one real edit/compile/error/run cycle;
- create a stable TP bank.

### P1 — MBASIC modern-key adapter Stage A

- generic adapter interface;
- MBASIC-specific FSM implementation;
- current-line shadow;
- adapter-initiated `Ctrl-A` transition;
- Left/Right/Home/End/Delete + printable insertion;
- validate Backspace carefully;
- preserve RAW escape hatch.

### P2 — MBASIC resynchronization / more states

- optionally recognize `EDIT n`, `EDIT .`, syntax-error auto Edit Mode via contextual output observation;
- only generalize the FSM machinery further if another real application demonstrates the need.

### P2+ — smart per-bank rendering

- first proof of concept syntax coloring in a simple BASIC/editor bank;
- then TP Pascal editor region;
- later richer selection/zoom/overview/TUI augmentation.

---

## 15. Testing philosophy

Keep automated tests focused on deterministic byte/state behavior. Do not overbuild tests before the major pieces exist.

Good automatic checks:

- selected bank/restored bank;
- `##` bypass exact bytes;
- history unchanged by HOST RAW traffic;
- BBC key -> documented byte map;
- MBASIC FSM transitions initiated by the adapter;
- `#9` vs `#9+##` transfer timing.

For substantial milestones, ask Petr to perform the real interactive tests on the device where keyboard feel, TFT rendering, editor behavior, menu layout, and visual correctness matter.

---

## 16. Historical/source notes used for this design

### Existing project observation

The attached project note `MBASIC terminal editing` records the locally confirmed Rev. 5.30 behavior: MBASIC owns console-character editing, `Ctrl-A` enters its `! ` Edit Mode, and static RunCPM/CCP cursor mappings are unsafe there.

### Microsoft BASIC-80 Release 5.0 Reference Manual

Section 2.16 documents the modal editor summarized above: Space/Rubout movement, `I`, `X`, `D`, `H`, `S`, `K`, `C`, CR/E/Q/L/A, and `Ctrl-A` entry from the line currently being typed. Rev. 5.30 observed behavior matches this model.

### BBC BASIC (Z80), CP/M manual

The CP/M configuration documentation states that cursor/editing key codes were not standardized and are patchable in `DIST.Z80`. The installed 5.00 package's source explicitly defines `^G/^O/^J/^L/^F/^N`, `^H`, `^R`, `^Q`, `^U`, and `^X`; these exact local definitions take precedence over older generic documentation.

### Turbo Pascal reference manual

The editor documentation states that secondary commands are WordStar-compatible and primary commands may be installed to suit the actual keyboard/terminal; early Turbo installation accepts key/escape sequences up to four characters.

### WordStar installation documentation

WordStar's CP/M installer explicitly contains custom terminal installation for the codes a terminal sends/receives and cursor-positioning behavior. This is a concrete example that full-screen terminal-owning software was normal CP/M software rather than a violation of the CP/M model.

---

## Bottom line

The goal is not to invent one mythical "CP/M modern keyboard" mapping. The correct 7xtgv abstraction is:

> **modern physical keyboard + smart local terminal mapped into the historical input/output model of the currently selected CP/M application.**

Most programs should need only a static bank map. BBC BASIC is one of these. Turbo Pascal/WordStar are full-screen but still largely static/configurable terminal cases. MBASIC is the exceptional modal editor that justifies a small stateful adapter. `##` provides the always-available ground-truth/raw escape hatch, and `#9` provides a stable Codex/test workspace.
