"""Small pre-upload target guard. No MAC registry and no device reset commands."""
import os
import re
import select
import termios
import time

QUERY = b'\x1b[>62v'  # existing snapshot query is harmless on older firmware
REPLY = re.compile(rb'\x1bP7xtgv-id;1;([a-z0-9-]{1,48});([0-9]{6}-[0-9]{4}|unstamped)\x1b\\')


class IdentityUnavailable(RuntimeError):
    pass


class TargetMismatch(RuntimeError):
    pass


def query_fd(fd, timeout=4):
    """Ask this open serial session; never trust cached mirror/banner text."""
    termios.tcflush(fd, termios.TCIFLUSH)
    deadline = time.monotonic() + timeout
    pending = QUERY
    received = bytearray()
    while time.monotonic() < deadline:
        readable, writable, _ = select.select(
            [fd], [fd] if pending else [], [], max(0, deadline - time.monotonic()))
        if writable:
            try:
                pending = pending[os.write(fd, pending):]
            except BlockingIOError:
                pass
        if readable:
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                continue
            if not chunk:
                raise IdentityUnavailable('Device disconnected during target query')
            received.extend(chunk)
            match = REPLY.search(received)
            if match and not pending:
                return tuple(value.decode('ascii') for value in match.groups())
            # Bound memory even when CP/M is continuously printing.
            if len(received) > 8192:
                del received[:-160]
    raise IdentityUnavailable('No firmware target reply (old, unavailable or unsupported firmware)')


def query_firmware(port):
    # Reuse the mirror's exclusive serial ownership and established DTR policy.
    from terminal import open_serial, close_serial
    fd, saved = open_serial(port)
    try:
        return query_fd(fd)
    finally:
        close_serial(fd, saved)


def require_target(port, expected, manual_target=None):
    try:
        actual, build = query_firmware(port)
    except IdentityUnavailable:
        if manual_target != expected:
            raise
        print(f'WARNING: explicitly accepting unidentified device as {expected}', flush=True)
        return
    if actual != expected:
        raise TargetMismatch(f'Refusing upload: device reports {actual} ({build}), '
                             f'but selected firmware target is {expected}')
    print(f'Confirmed firmware target: {actual}, running build {build}', flush=True)
