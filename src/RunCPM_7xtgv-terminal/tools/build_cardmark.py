#!/usr/bin/env python3
"""Build CARDMARK with DRI ASM/LOAD in a disposable native RunCPM drive."""
import os
from pathlib import Path
import pty
import shutil
import subprocess
import tempfile
from test_vt_guest_asm import ROOT, DRIVE, ASM, RUNCPM, until_prompt


def main():
    with tempfile.TemporaryDirectory(prefix='xtgv-cardmark-') as temp:
        drive = Path(temp) / 'A/0'
        drive.mkdir(parents=True)
        for source in (ASM, DRIVE / 'LOAD.COM', DRIVE / 'CARDMARK.ASM'):
            shutil.copyfile(source, drive / source.name)
        master, slave = pty.openpty()
        process = subprocess.Popen([str(RUNCPM)], cwd=temp, stdin=slave,
                                   stdout=slave, stderr=slave,
                                   env=dict(os.environ, TERM='xterm'), start_new_session=True)
        os.close(slave)
        try:
            until_prompt(master)
            for command in ('ASM CARDMARK', 'LOAD CARDMARK'):
                os.write(master, (command + '\r').encode())
                output = until_prompt(master)
                if b'ERROR' in output.upper() or b'?' in output:
                    raise RuntimeError(output.decode(errors='replace'))
            if not (drive / 'CARDMARK.COM').is_file():
                raise RuntimeError(f'No COM generated: {[p.name for p in drive.iterdir()]}')
            os.write(master, b'CARDMARK\r')
            output = until_prompt(master)
            if b'requires supported firmware' not in output:
                raise RuntimeError(f'Unsupported firmware detection failed: {output!r}')
            shutil.copyfile(drive / 'CARDMARK.COM', DRIVE / 'CARDMARK.COM')
            os.write(master, b'EXIT\r')
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)
    print('CARDMARK.COM built; unsupported-host path tested; no device transfer')


if __name__ == '__main__':
    main()
