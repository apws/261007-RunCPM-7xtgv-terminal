"""Adjacent launcher transcript; detailed per-session/build evidence is retained."""
import contextlib
import os
from pathlib import Path
import sys
import time


def append_log(path, text):
    if path:
        with Path(path).open('a', encoding='utf-8') as log:
            log.write(text)


@contextlib.contextmanager
def shortcut_log(path):
    if not path:
        yield
        return
    path = Path(path)
    # windows.cmd already truncates its launcher's log and writes bootstrap info.
    if not os.environ.get('XTGV_SHORTCUT_LOG'):
        path.write_text('', encoding='utf-8')
    with path.open('a', encoding='utf-8') as log:
        log.write(f'\n*** {time.strftime("%F %T")} ***\n'); log.flush()
        class Tee:
            def __init__(self, stream):
                self.stream = stream
            def write(self, text):
                self.stream.write(text); log.write(text); log.flush()
                return len(text)
            def flush(self):
                self.stream.flush(); log.flush()
        with contextlib.redirect_stdout(Tee(sys.stdout)), contextlib.redirect_stderr(Tee(sys.stderr)):
            try:
                yield
            except BaseException as error:
                log.write(f'\nFAILED: {error}\n'); log.flush()
                raise
