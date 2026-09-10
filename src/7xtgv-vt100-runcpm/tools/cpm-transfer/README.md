# Console file transfer

VTGET.COM and VTPUT.COM are tiny CP/M-80 guest utilities. They move 128-byte
records as hexadecimal pages through the interpreted terminal mirror, with
acknowledgements between pages/chunks. No raw binary serial mode or firmware
changes are required. VTPUT replaces the named destination; the Python sender
then reads the complete file back and compares every byte. CP/M record padding
is retained. This is a testing utility, not an emulator feature.

During uploads, the Python tool tracks one total across every selected file and
the automatic read-back pass. The CP/M helpers show the current 8.3 filename and
that percentage on the device TFT. Percentage text is sent as an optional suffix
or acknowledgement, so the Python tool can bootstrap an older helper and the new
helpers still accept the original protocol.

Build with the z80asm assembler (AlbertVeli/z80asm upstream):

```
z80asm -o VTGET.COM VTGET.ASM
z80asm -o VTPUT.COM VTPUT.ASM
```

Install both COM files in A0. RunCPM's command lookup can find them from other
user areas. From the project directory, with the serial console idle at CP/M:

```
python3 tools/cpm-transfer/transfer.py --user 1 get BBCBASIC.COM --directory /tmp/bbc-copy
python3 tools/cpm-transfer/transfer.py --user A put /path/to/FILE.COM
```

`--user` uses the same hexadecimal suffix as `cpmdrive`: `0` through `F`.
Thus `--user A` means folder `AA - ...`; the tool translates it to RunCPM's
decimal `A10:` command on the wire.

The helpers use the default command FCB and BDOS file/console calls. Receiver
input is uppercase hex, exactly 64 digits per requested chunk. `!` at a record
boundary ends an upload. The reader tags both the beginning and end of each
page with its record number so coalesced screen updates cannot complete a
partially refreshed page. The tool does not read a physically independent TFT
framebuffer; its output is the firmware's serial mirror.
