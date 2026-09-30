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
import tempfile
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
            # Initial attachment requests the dedicated mirror-only snapshot;
            # it must not issue the older full terminal/TFT reset command.
            until(board_master, b'\x1b[>62v')
            until(pc_master, b'\x1b[?1016l')
            os.write(pc_master, b'DIR\r\x03')
            until(board_master, b'DIR\r\x03')
            os.write(pc_master, b'\x1b[<64;12;34M\x1b[M`23\x1b[96;12;34M\x1b[A')
            keyboard = until(board_master, b'\x1b[A')
            self.assertEqual(keyboard, b'\x1b[A')
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

    def test_reconnects_when_serial_node_is_replaced(self):
        pc_master, pc_slave = pty.openpty()
        board1_master, board1_slave = pty.openpty()
        board2_master, board2_slave = pty.openpty()
        proc = None
        with tempfile.TemporaryDirectory() as temporary:
            port = os.path.join(temporary, 'ttyACM0')
            os.symlink(os.ttyname(board1_slave), port)
            try:
                fcntl.ioctl(pc_slave, termios.TIOCSWINSZ,
                            struct.pack('HHHH', 24, 80, 0, 0))
                proc = subprocess.Popen([sys.executable, '-c', CHILD, HELPER, port],
                                        stdin=pc_slave, stdout=pc_slave, stderr=pc_slave)
                until(board1_master, b'\x1b[>62v')
                until(pc_master, b'\x1b[?1016l')
                os.unlink(port)
                os.symlink(os.ttyname(board2_slave), port)
                until(board2_master, b'\x1b[>62v')
                until(pc_master, b'\x1b[?1016l')
                os.write(board2_master, b'\x1b[2J\x1b[HRECONNECTED')
                until(pc_master, b'RECONNECTED')
                os.write(pc_master, b'\x1dq')
                proc.wait(timeout=3)
                self.assertEqual(proc.returncode, 0)
            finally:
                if proc and proc.poll() is None:
                    proc.kill()
                    proc.wait()
                for fd in (pc_master, pc_slave, board1_master, board1_slave,
                           board2_master, board2_slave):
                    os.close(fd)

if __name__ == '__main__':
    unittest.main()
