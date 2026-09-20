# 260921-00 — FAND 1.0 on SHARP MZ-821: likely Lamač / LEC CP/M console lineage

## Context

During the FAND 1.0 bring-up under 7XTGV RunCPM, the original FANDINST preset identifies the target as:

```text
SHARP MZ-821
```

and FAND itself displays:

```text
FAND-SHARP MZ 821 & K 6314(13)
```

The first MZ-800 CP/M source found for comparison was the Czech annotated `BIOS8MZ.MAC` listing for CP/M V2.26-31. Its console-control table does **not** match the FANDINST `SHARP MZ-821` preset closely enough to be the console personality FAND was installed for.

The important distinction therefore appears not to be MZ-800 versus MZ-821 hardware, but **which Czech CP/M implementation / BIOS / terminal layer was used on the same MZ-800-family machine**.

---

## FANDINST console personality seen in the original SHARP MZ-821 preset

The relevant original FANDINST values are:

```text
Columns / rows:          80 / 25

Alternate brightness:
    ON                   1C 33 00
    OFF                  1C 32 00

Blink:
    ON                   00 00 00
    OFF                  00 00 00

Inverse:
    ON                   1F 00 20
    OFF                  20 1F 00

Cursor-address lead-in:  1B 00 00 00 00
Between row / column:    00 00 00 00 00
After row / column:      00 00 00 00 00
Column first:            0
Row / column offset:     128 / 128
Binary addressing:       1
ASCII cursor digits:     0

Clear screen:            0C 00 00 00 00
Cursor homes after clear:1
Home command:            00 00 00 00 00
Erase to end of line:    16 00 00 00 00
```

So the apparent terminal language expected by this FAND installation is approximately:

```text
CLEAR        0C
EOL erase    16
CURSOR       1B row+80h col+80h
BRIGHT ON    1C 33
BRIGHT OFF   1C 32
INVERSE ON   1F 00 20
INVERSE OFF  20 1F 00
```

This is materially different from the console table in the CP/M V2.26-31 `BIOS8MZ.MAC` source. In that BIOS, for example, `1Fh` is used for clear-screen and `16h` is not the FAND erase-to-EOL operation.

That is not a small setup variation; it indicates a substantially different console protocol.

---

## Stronger candidate: Jiří Lamač CP/M / LEC CP/M

A surviving distribution is described as:

> 60K Personal CP/M for Sharp MZ-800 Version 1.4, written by Jiří Lamač in 1988, Czech version.

Archive page:

- https://r3tr0.de/mirror/sharpmz.org/mz-800/dldos.htm

Contemporary Czech Sharp material describes Lamač's system as the commonly used Czech CP/M branch, with Czech support and improved disk support, while the original Sharp P-CP/M was largely retained for compatibility / historical reasons.

That same material distinguishes disk images such as:

```text
lamac_1.dsk    bare / classic Lamač CP/M
lamac_2.dsk    Lamač CP/M plus the normal utility set
```

A useful surviving contemporary source is:

- https://cz.doczz.net/doc/70965/d%C3%ADvejte-se-na-televizi-na-inte

This makes Lamač / LEC CP/M a much stronger candidate for the environment in which the FAND `SHARP MZ-821` preset was created.

---

## SCAV `CPM1` directory looks closely related to the Lamač utility set

The SCAV MZ-800 software archive contains:

```text
/MZ-800_Software/COM/PROGS/CPM1/
```

with files including:

```text
CLS.COM          128 bytes
HOME.COM         256 bytes
TERMINAL.COM     128 bytes
DEFKEY.COM       384 bytes
KEYB.COM         640 bytes
SETUP.COM        ~28 KB
SETUP1.COM       ~19 KB
DISKDEF.COM
COPYDISK.COM
COPYSYS.COM
FORMAT.COM
...
```

Archive index:

- https://mz-800.scav.cz/download/MZ-800/download_MZ-800_software.html

This utility collection corresponds remarkably well to the tools described for the Lamač `lamac_2.dsk` system: `CLS`, `COPYDISK`, `COPYSYS`, `DEFKEY`, `DISKDEF`, `FORMAT`, and the `SETUP.COM` configurator.

It is not yet proven that SCAV's `CPM1` directory is a byte-for-byte extraction of `lamac_2.dsk`, but the relationship is strong enough to make it the primary investigation target.

### Especially interesting: `TERMINAL.COM`

`TERMINAL.COM` is only **128 bytes**, exactly one CP/M record.

That makes it a very attractive first disassembly target. It may:

- select / patch a terminal personality,
- modify a jump table,
- change console mode,
- install a small resident hook,
- or perform some other tiny configuration step.

We must **not** yet assume that it directly implements the whole FAND console protocol, but a one-record executable named `TERMINAL.COM` inside what appears to be the Lamač CP/M utility set is highly relevant.

The other small files worth examining immediately are:

```text
CLS.COM
HOME.COM
DEFKEY.COM
KEYB.COM
```

and then the larger `SETUP.COM` / `SETUP1.COM` binaries for tables or configuration data involving:

```text
0C
16
1B
1C
1F
20
80h
```

---

## `K 6314(13)` is almost certainly a printer reference, not a CP/M version

The FAND identification string:

```text
FAND-SHARP MZ 821 & K 6314(13)
```

initially looked as though `K 6314(13)` might identify a system or BIOS revision.

A better explanation is that **Robotron K6314 is a real dot-matrix printer**, related to the K6313 family and used in Czechoslovak / East German environments.

Reference:

- https://www.robotrontechnik.de/html/drucker/k631x.htm

So the FAND string most likely means approximately:

```text
Sharp MZ-821 console + K6314 / K6313 printer configuration
```

The exact meaning of the parenthesized `(13)` is not yet established, but this makes the string much less useful as a CP/M-version clue.

---

## Another potentially decisive source-code route: HDS / original Lamač sources

In 2009, Ján Koštial (Lucky-Soft / Tatra-Softer) described an archive `HDS.ZIP` and stated that his HDS system originated from Jiří Lamač's original CP/M.

He also said that the recovered disk contained source files for:

- the original CP/M,
- HDS,
- and helper programs,

distributed across CP/M USER areas 0–15.

He explicitly mentioned a source file named:

```text
FYZRW.HDS
```

The surviving mailing-list archive is:

- https://mz-800.scav.cz/upload/Archiv_Konference_Pocitac_SHARP_MZ-800_a_emulatory_2009.pdf

The separately indexed `HDS.ZIP` attachment has not yet been recovered in this investigation, but if found it may contain the most direct surviving route to the **original Lamač CP/M sources**.

This is now a high-value archival target.

---

## Current interpretation

The evidence now fits this lineage much better:

```text
Sharp MZ-821
    ↓
Jiří Lamač / LEC CP/M
    ↓
its console / terminal personality
    ↓
FAND 1.0 "SHARP MZ-821" preset
```

rather than:

```text
Sharp MZ-821
    ↓
CP/M V2.26-31 / BIOS8MZ.MAC
    ↓
FAND
```

There were clearly multiple CP/M implementations on essentially the same Sharp hardware.

Therefore the V2.26-31 `BIOS8MZ.MAC` console should **not** currently be treated as the definition of an MZ-821 terminal personality for 7XTGV.

---

## Consequence for 7XTGV / RunCPM

Do **not** implement an `output=mz821` decoder by copying the V2.26-31 `BIOS8MZ.MAC` control table.

If FAND ultimately requires a Sharp-specific output personality, it should emulate the **actual Lamač / LEC console protocol used by the FAND installation**, established from one or more of:

1. real Lamač CP/M binaries / source,
2. `TERMINAL.COM` / `SETUP.COM` disassembly,
3. the recovered HDS / Lamač sources,
4. controlled byte traces from FAND on the matching system,
5. or another primary-source description of that console driver.

Until then, the existing ANSI adaptation remains the safer experimental route.

---

## Immediate next investigation steps

1. Obtain the Lamač CP/M 1.4 disk image.
2. Extract its filesystem.
3. Compare file names and hashes against SCAV `CPM1`.
4. Disassemble `TERMINAL.COM` first.
5. Inspect `CLS.COM`, `HOME.COM`, `DEFKEY.COM`, and `KEYB.COM`.
6. Search `SETUP.COM` / `SETUP1.COM` for terminal-control constants and tables.
7. Search specifically for the FAND control bytes:
   - `0C`
   - `16`
   - `1B`
   - `1C 32`
   - `1C 33`
   - `1F 00 20`
   - `20 1F 00`
   - binary cursor bytes with `+80h` offsets.
8. Recover the historical `HDS.ZIP` if possible and inspect the claimed original Lamač CP/M source files.
9. Only after this evidence is collected decide whether 7XTGV needs a dedicated `mz821` / `lamac` output personality.

---

## Working conclusion

The earlier discovery of an MZ-800 CP/M source was useful, but it appears to be **the wrong CP/M lineage for this FAND package**.

The strongest current hypothesis is that the original FAND 1.0 `SHARP MZ-821` profile was configured for **Jiří Lamač / LEC CP/M, probably the 1.3/1.4 lineage**, whose console personality differs materially from the V2.26-31 `BIOS8MZ.MAC` implementation.

The smallest and most immediately actionable artifact is:

```text
TERMINAL.COM    128 bytes
```

from the SCAV `CPM1` utility set.

That should be the first binary placed on the dissection table.
