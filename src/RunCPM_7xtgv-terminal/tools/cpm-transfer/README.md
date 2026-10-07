# Console file transfer

VTGET.COM and VTPUT.COM are tiny CP/M-80 guest utilities. They move 128-byte
records as hexadecimal pages through the interpreted terminal mirror, with
acknowledgements between pages/chunks. No raw binary serial mode or firmware
changes are required. VTPUT replaces the named destination; with `--verify`,
the Python sender reads the complete file back and compares every byte. CP/M record padding
is retained. This is a testing utility, not an emulator feature.

Use an **80-column logical terminal** for transfers. A 40-column logical
terminal wraps VTGET's 69-character tagged rows and can stall a download.
A smaller physical viewport onto an 80-column logical terminal is fine;
viewport width and protocol line width are different. RAW input bypasses the
usual pan-command interception, so interactive pan is not transfer recovery.

Before editing the device configuration, fetch current A0:7XTGVTRM.INI with
`tools/fetch-7xtgvtrm-ini.sh` (supports explicit `--target` and `--port`), edit
that fresh mirror, and send only with overwrite approval. A failed fetch must
not be followed by sending an older local INI. The helper archives the prior
local copy and replaces it only on successful download.

During uploads, the Python tool tracks one total across every selected file and
the automatic read-back pass. The CP/M helpers show the current 8.3 filename and
that percentage on the device TFT. Percentage text is sent as an optional suffix
or acknowledgement, so the Python tool can bootstrap an older helper and the new
helpers still accept the original protocol.

The source suffix is intentional: these files contain Z80 mnemonics. SLR
`Z80ASM NAME` on CP/M defaults to `NAME.Z80` and directly emits `NAME.COM`;
Digital Research `ASM` defaults to `.ASM` and is not compatible with them.

Build on the host with the z80asm assembler (AlbertVeli/z80asm upstream):

```
z80asm -o VTGET.COM VTGET.Z80
z80asm -o VTPUT.COM VTPUT.Z80
```

For z88dk-z80asm, assemble temporary copies with multi-character single-quoted
strings/include filenames converted to double quotes, using `-b -oNAME.COM`.
Keep the project Z80 sources in their existing dialect.

Install both COM files in A0. RunCPM's command lookup can find them from other
user areas. From the project directory, with the serial console idle at CP/M:

```
python3 tools/cpm-transfer/transfer.py --user 1 get BBCBASIC.COM --directory /tmp/bbc-copy
python3 tools/cpm-transfer/transfer.py --user A put /path/to/FILE.COM
```

`--user` uses the RunCPM hexadecimal user-directory convention: `0` through `F`.
Thus `--user A` means user 10 (card directory `A/A`); the tool translates it to RunCPM's
decimal `A10:` command on the wire.

The transfer helper reads the selected profile using Alt+Space's `CSI >14v`
notice, then temporarily selects reserved bank `#9 GENERAL`, keeping interactive
histories untouched. It enters `## RAW` for transfer data. Cleanup restores the
original profile and RAW state on success or failure, without executing the
profile's `on_select` command or changing device DU. The restored notice is
checked; a disconnected device or unconfirmed restoration reports an error.
If the initial profile cannot be read, it aborts before selecting #9.
Use `--no-raw` to retain normal #9 processing for comparison testing; the
original profile/RAW state is still restored.

The helpers use the default command FCB and BDOS file/console calls. Receiver
input is uppercase hex, exactly 64 digits per requested chunk. `!` at a record
boundary ends an upload. The reader tags both the beginning and end of each
page with its record number so coalesced screen updates cannot complete a
partially refreshed page. The tool does not read a physically independent TFT
framebuffer; its output is the firmware's serial mirror.

Since 260923 the helpers clear the full screen once, not per chunk/record.
VTPUT homes the cursor and clears only the old echoed input line. VTGET uses
`R2Cnnnn` headers and appends `:nnnn` to each of its four hex rows, in addition
to `READYnnnn`. The host requires all row tags to match before acknowledging:
the embedded mirror can send changed rows in rotating order, so header/footer
alone are insufficient without the old clear. New hosts accept legacy `REC`
pages too; old hosts must be updated before installing the new VTGET.

The mirror's Ctrl+Shift+X / Ctrl+] m panel uses this same Link over its existing
descriptor, never a second reader. It checks for idle CCP, asks local DU, Source,
then Target (prefilled from Source). The device stays in its current DU.
Send confirms every remote write; Receive confirms an existing local target.
Y proceeds, N edits Target, C/Esc cancels. Results remain until dismissed.
No XMODEM bytes are sent by that panel. The separate CLI `--user` option still
explicitly switches the device to A plus that user; it is not the UI's local DU.
The idle check accepts numeric CCP prompts and the bounded `7XF DN`
`/drive/area>` form. After a CLI `--user` change, an exact numeric prompt
confirms the selected DU directly. A named `7XF DN` prompt temporarily
switches to `7XF DD` to confirm the number, then restores `DN` before the
transfer. A drive-only prompt still checks bare `CD` for the numeric DU; it
accepts both old `A7:` and new `A7: /a/mbasic` output while devices are
upgraded.


Uploads are one-way by default: `cpmcard/` is the primary working source and VTPUT
reports its save result. Add `--verify` only when a byte-for-byte device
read-back is worth the extra terminal traffic.
