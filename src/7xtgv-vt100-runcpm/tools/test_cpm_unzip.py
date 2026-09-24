#!/usr/bin/env python3
"""Compare local CP/M UNZIP binaries with Linux zip in disposable storage."""
import os
from pathlib import Path
import pty
import shutil
import subprocess
import tempfile
import time

from build_vtpal import ROOT, CORE, receive


def main():
    payload = (CORE/'7XTGVTRM.INI').read_bytes()
    for binary in ('UNZIP.COM', 'UNZIP157.COM'):
        for level in (0, 1, 9):
            with tempfile.TemporaryDirectory(prefix='xtgv-unzip-') as temporary:
                base = Path(temporary)
                drive = base/'A/0'
                drive.mkdir(parents=True)
                shutil.copyfile(CORE/binary, drive/binary)
                source = base/'VTINI.INI'
                source.write_bytes(payload)
                archive = drive/'VTZIP.ZIP'
                subprocess.run(['zip', f'-{level}', '-j', str(archive), str(source)],
                               check=True, stdout=subprocess.DEVNULL)
                # Actual transfers pad to 128-byte records. Include that boundary.
                archive_size = archive.stat().st_size
                with archive.open('ab') as output:
                    output.write(bytes((-archive_size) % 128))
                master, slave = pty.openpty()
                process = subprocess.Popen([str(ROOT/'build/linux_RunCPM/RunCPM')],
                    cwd=base, stdin=slave, stdout=slave, stderr=slave,
                    env=dict(os.environ, TERM='xterm'), start_new_session=True)
                os.close(slave)
                try:
                    receive(master)
                    started = time.monotonic()
                    os.write(master, (binary[:-4]+' VTZIP.ZIP A:\r').encode())
                    try:
                        report = receive(master, timeout=15)
                        extracted = drive/'VTINI.INI'
                        actual = extracted.read_bytes() if extracted.exists() else b''
                        # CP/M file length rounds to records; only its tail is padding.
                        okay = actual[:len(payload)] == payload and len(actual) == (len(payload)+127)//128*128
                        print(f'{binary} zip -{level}: {archive_size} bytes; '
                              f'{"PASS" if okay else "FAIL"}; native extraction {time.monotonic()-started:.3f}s')
                        print(report.decode('ascii', errors='replace').strip())
                        if binary == 'UNZIP157.COM':
                            assert okay, 'Maintained extractor did not reproduce the input'
                    except RuntimeError as exc:
                        print(f'{binary} zip -{level}: {exc}')
                        if binary == 'UNZIP157.COM':
                            raise
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=5)
                    os.close(master)


if __name__ == '__main__':
    main()
