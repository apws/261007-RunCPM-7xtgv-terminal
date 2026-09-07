"""Exercise real TTY I/O; only USB modem ioctls are unavailable on pseudo-TTYs."""
import errno
import fcntl
import os
import pathlib
import pty
import select
import struct
import subprocess
import sys
import termios
import time
import unittest

HELPER = str(pathlib.Path(__file__).parents[1] / 'tools' / 'terminal.py')
CHILD = r'''
import importlib.util, fcntl, termios, sys
spec=importlib.util.spec_from_file_location('helper', sys.argv[1])
h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
original=fcntl.ioctl
def ioctl(fd, command, *args):
    if command in (termios.TIOCMBIS, termios.TIOCMBIC): return 0
    return original(fd, command, *args)
fcntl.ioctl=ioctl
h.run(sys.argv[2], None)
'''


def until(fd, marker, timeout=3):
    result = bytearray()
    deadline = time.monotonic() + timeout
    while marker not in result and time.monotonic() < deadline:
        if select.select([fd], [], [], 0.05)[0]:
            try:
                result.extend(os.read(fd, 8192))
            except OSError as exc:
                if exc.errno == errno.EIO:
                    break
                raise
    if marker not in result:
        raise AssertionError(f'Missing {marker!r} in {result!r}')
    return result


class HelperIntegration(unittest.TestCase):
    def test_actual_serial_keyboard_mirror_and_cleanup(self):
        pc_master, pc_slave = pty.openpty()
        board_master, board_slave = pty.openpty()
        proc = None
        original = termios.tcgetattr(pc_slave)
        try:
            fcntl.ioctl(pc_slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
            proc = subprocess.Popen([sys.executable, '-c', CHILD, HELPER, os.ttyname(board_slave)],
                                    stdin=pc_slave, stdout=pc_slave, stderr=pc_slave)
            until(board_master, b'\x1b[>0v')
            os.write(pc_master, b'DIR\r\x03')
            until(board_master, b'DIR\r\x03')
            os.write(board_master, b'\x1b[2J\x1b[HCP/M MIRROR\r\nA0>')
            until(pc_master, b'CP/M MIRROR\r\nA0>')
            os.write(pc_master, b'\x1d2\x1da')
            until(board_master, b'\x1b[>2v\x1b[1;3H')
            os.write(pc_master, b'\x1dq')
            proc.wait(timeout=3)
            self.assertEqual(proc.returncode, 0)
            until(pc_master, b'\x1b[?1049l')
            self.assertEqual(termios.tcgetattr(pc_slave), original)
        finally:
            if proc and proc.poll() is None:
                proc.kill()
                proc.wait()
            for fd in (pc_master, pc_slave, board_master, board_slave):
                os.close(fd)

if __name__ == '__main__':
    unittest.main()
