#!/usr/bin/env python3
"""Check profile-exit command handling in BASIC tools on disposable CP/M cards."""
import os
from pathlib import Path
import pty
import shutil
import subprocess
import tempfile
import time

from build_vtpal import ROOT, receive
from card_paths import card_root


def check_exit(program, choice=None, command=b"\rsystem\r"):
    source = card_root() / 'C/1'
    with tempfile.TemporaryDirectory(prefix='xtgv-basic-exit-') as temporary:
        drive = Path(temporary) / 'A/0'
        drive.mkdir(parents=True)
        for name in ('MBASIC.COM', program + '.BAS'):
            shutil.copyfile(source / name, drive / name)
        master, slave = pty.openpty()
        process = subprocess.Popen([str(ROOT / 'build/RunCPM_linux/RunCPM')],
            cwd=temporary, stdin=slave, stdout=slave, stderr=slave,
            env=dict(os.environ, TERM='xterm'), start_new_session=True)
        os.close(slave)
        try:
            receive(master)
            os.write(master, f'MBASIC {program}\r'.encode())
            prompt = b'Choose preferred preset last.' if program == 'VTPAL' else b'Select: '
            receive(master, prompt)
            if program == 'VTPAL':
                # A lone s is not an exit alias; a normal redraw still works.
                os.write(master, b's')
                time.sleep(0.03)
                os.write(master, b'r')
                receive(master, prompt)
                os.write(master, b'\r')
                time.sleep(0.03)
            if choice is not None:
                os.write(master, f'{choice}\r'.encode())
                prompt = {4: b'Enter: all 32 glyphs ', 5: b'Enter: return to ANSI ',
                          9: b'Enter: next scroll check '}.get(choice, b'Enter returns to menu: ')
                receive(master, prompt)
            for byte in command:
                os.write(master, bytes([byte]))
                time.sleep(0.03)
            result = receive(master, b'OK\r')
            assert b'error' not in result.lower() and b'Break' not in result, result
            assert b'A0>' not in result, result
            assert b'\x1b[?25h' in result and b'\x1b[0m' in result, result
            if program == 'VTESTS':
                assert b'\x1b<' in result and b'\x1b[r' in result, result
            # A subsequent command proves SYSTEM was consumed completely,
            # rather than left queued to exit the interpreter after END.
            os.write(master, b'PRINT 12345\r')
            result = receive(master, b'OK\r')
            assert b' 12345 ' in result and b'A0>' not in result, result
            for byte in b'SYSTEM\r':
                os.write(master, bytes([byte]))
                time.sleep(0.03)
            receive(master)
            os.write(master, b'EXIT\r')
            assert process.wait(timeout=10) == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            os.close(master)


def main():
    cases = 0
    # The profile can erase a partial line, start a fresh line and inject
    # system + CR. Test both that stream and plain/mixed-case commands.
    for command in (b'\b\b\rsystem\r', b'SYSTEM\r', b'\rSyStEm\r'):
        for program in ('VTPAL', 'VTESTS'):
            check_exit(program, command=command)
            cases += 1
    for choice in range(1, 10):
        check_exit('VTESTS', choice, b'\rsystem\r')
        cases += 1
    print(f'PASS: {cases} MBASIC profile-exit cases; cleanup, OK prompt and no queued SYSTEM.')


if __name__ == '__main__':
    main()
