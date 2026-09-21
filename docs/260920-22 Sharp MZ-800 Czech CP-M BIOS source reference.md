# Czech Sharp MZ-800 CP/M BIOS source reference

The primary source located for this investigation is the scanned Czech
*Komentované zdrojové texty CP/M*:
[PDF, 240 pages](https://sam.speccy.cz/olddocs/cp-m_koment-zdroj.pdf).
Its `BIOS8MZ.MAC` listing starts on PDF page 200 (book page 190) and is headed
`BIOS FOR CP/M V2.26-31 BIOS MZ-800`. The listing credits Pavel Zemčík
(keyboard/display, 1987), Tomáš Krejča (floppy disks, 1988), and Miroslav
Beneš (keyboard/display revision, 1988). This is source for **one specific
MZ-800 CP/M BIOS**, not proof of the firmware or console protocol used by
every MZ-821 installation. A contemporary
[Sharp MZ-821 brochure](https://www.idealine.info/sharpmz/download/allesfuersharp2_85.pdf)
describes optional P-CP/M support, but does not identify this particular
BIOS build.

Useful entry points in the PDF:

- [PDF page 200](https://sam.speccy.cz/olddocs/cp-m_koment-zdroj.pdf#page=200):
  listing identity, credits, BIOS jump table, including `CONIN`, `CONOUT`,
  `SETSEC`, `READ`, `WRITE`, and `SECTRAN`.
- [PDF page 210](https://sam.speccy.cz/olddocs/cp-m_koment-zdroj.pdf#page=210):
  display output routine; its `CONO` masks the incoming byte with `7Fh`
  before interpreting it.
- [PDF page 215](https://sam.speccy.cz/olddocs/cp-m_koment-zdroj.pdf#page=215):
  control-byte dispatch table. For this BIOS, `14h`/`15h` toggle inverse,
  `1Bh` begins cursor addressing, `1Ch` homes, and `1Fh` clears the screen.
  `16h` (insert line) is explicitly ignored; this listing does **not** call
  it erase-to-EOL.
- [PDF pages 220–225](https://sam.speccy.cz/olddocs/cp-m_koment-zdroj.pdf#page=220):
  keyboard scanning and conversion into CP/M console input.

Do not treat this table as a universal Sharp terminal protocol: a different
CP/M distribution or board revision could use different console controls.
Identify the exact port and observe the console byte stream before using it
to implement an emulation mode. The listing is valuable as historical source
and as an example of where a machine's BIOS could transform control and
keyboard bytes.

For FAND-specific installer values, binary observations, and test results,
keep using the local/private `MIXWORX` FAND log under the terminal manual;
that log is intentionally not published with the repository.
