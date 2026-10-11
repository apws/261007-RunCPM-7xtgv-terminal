"""Own the native key hook for one uniquely named Mintty window/thread."""
from pathlib import Path
import os
import queue
import subprocess
import sys
import threading

from mintty_profile import window_class
from windows_build import native_path

from host_key_config import TOPROW_SETTING, keyboard_environment as mintty_environment


class KeyBridge:
    def __init__(self, build, profile):
        self.build = Path(build)
        self.profile = Path(profile)
        self.child = None
        self.log = None

    def __enter__(self):
        if sys.platform != 'cygwin':
            return self  # Linux protocol tests do not install a Windows hook.
        binary = self.build / 'xtgv_key_bridge.exe'
        library = self.build / 'xtgv_key_hook.dll'
        for path in (binary, library):
            if not path.is_file():
                raise RuntimeError(f'Windows key helper missing: {path}. Run this target\'s desktop-rebuild.cmd.')
        self.log = (self.profile.parent / 'key-bridge.log').open('wb')
        try:
            row = mintty_environment()[TOPROW_SETTING]
            self.log.write((TOPROW_SETTING + '=' + repr(row) + '\n').encode('utf-8'))
            self.log.flush()
            encoded_row = ''.join(f'{ord(char):04x}' for char in row) or '-'
            self.child = subprocess.Popen([str(binary), window_class(self.profile), native_path(library), encoded_row],
                                          stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                          stderr=self.log, start_new_session=True)
            ready = queue.Queue()
            reader = threading.Thread(target=lambda: ready.put(self.child.stdout.readline()), daemon=True)
            reader.start()
            try:
                line = ready.get(timeout=12)
            except queue.Empty:
                raise RuntimeError('Windows key helper startup timed out; see key-bridge.log') from None
            if line.strip() != b'READY':
                raise RuntimeError('Windows key helper failed; see key-bridge.log')
            self.log.write(b'Active: thread-scoped physical number-row/keypad helper\n')
            self.log.flush()
            return self
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.child is not None:
            if self.child.stdin:
                try:
                    self.child.stdin.close()  # EOF requests UnhookWindowsHookEx.
                except OSError:
                    pass
            try:
                self.child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                # Only our private helper is terminated, never the desktop/card owner.
                self.child.terminate()
                self.child.wait(timeout=5)
            self.child.stdout.close()
            self.child = None
        if self.log is not None:
            self.log.write(b'Stopped\n')
            self.log.close()
            self.log = None

    def __exit__(self, *exc):
        self.close()
