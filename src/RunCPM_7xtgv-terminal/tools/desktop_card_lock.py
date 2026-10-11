"""One cooperating desktop writer per card, also across a shared filesystem."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import secrets
import socket

LOCK_NAME = '.7xtgv-desktop-lock'


class CardLease:
    # An unresponsive child may still be writing; leave evidence for recovery.
    retained = False

    def retain(self):
        self.retained = True


@contextmanager
def card_lock(root):
    directory = Path(root).resolve(strict=True) / LOCK_NAME
    token = secrets.token_hex(16)
    try:
        directory.mkdir()
    except FileExistsError:
        raise RuntimeError(f'Card already locked: {directory}. Close its desktop session first. '
                           'After a crashed session, verify that its emulator has stopped before removing this lock.') from None
    owner = directory/'owner.json'
    lease = CardLease()
    try:
        owner.write_text(json.dumps({'host': socket.gethostname(), 'pid': os.getpid(), 'token': token})+'\n')
        yield lease
    finally:
        # Do not erase a lock replaced by another owner, or any unrelated files.
        if not lease.retained and owner.exists() and json.loads(owner.read_text()).get('token') == token:
            owner.unlink()
            directory.rmdir()
