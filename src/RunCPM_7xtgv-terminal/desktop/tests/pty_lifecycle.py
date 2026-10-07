"""Actual mirror protocol over a PTY; guest execution and owner shutdown."""
import importlib.util
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time

SKETCH = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SKETCH / 'tools'))
from card_paths import card_root, require_files
os.environ['XTGV_DESKTOP_TRANSPORT'] = 'pty'
spec = importlib.util.spec_from_file_location('smoke', SKETCH / 'tests/hardware_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)

def scenario(mode):
    with tempfile.TemporaryDirectory(prefix='xtgv-pty-card-') as directory:
        root = Path(directory)
        area = root / 'A/0'
        area.mkdir(parents=True)
        (area / 'VTLOOP.COM').write_bytes(bytes.fromhex('c30001'))
        # BDOS console output in an endless loop; exercises full output queues.
        (area / 'VTFLOOD.COM').write_bytes(bytes.fromhex('1e580e02cd0500c30001'))
        (area / 'VTREBOOT.COM').write_bytes(bytes.fromhex('3e01d37cc9'))
        cardmark = card_root() / 'A/0/CARDMARK.COM'
        require_files(cardmark)
        shutil.copyfile(cardmark, area / 'CARDMARK.COM')
        process = subprocess.Popen([sys.argv[1], '--card-root', str(root), '--pty',
                                    '--run-for-ms', '8000'], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        fd = None
        screen = smoke.Mirror(80)
        try:
            assert process.stdout.readline().startswith('Desktop ')
            endpoint = process.stdout.readline().strip().removeprefix('PTY=')
            fd, saved = smoke.bridge.open_serial(endpoint)
            def pump():
                if select.select([fd], [], [], .05)[0]:
                    data = os.read(fd, 16384)
                    if data: screen.feed(data)
            def expect(text):
                until = time.monotonic() + 5
                while text not in screen.text() and time.monotonic() < until:
                    pump()
                assert text in screen.text(), (text, screen.text())
            expect('A0>')
            os.write(fd, b'CARDMARK\r')
            expect('CARDMARK: requires supported firmware')
            # Reattach to the same local endpoint; existing snapshot path.
            smoke.bridge.close_serial(fd, saved); fd = None
            time.sleep(.05)
            screen = smoke.Mirror(80)
            fd, saved = smoke.bridge.open_serial(endpoint)
            os.write(fd, smoke.bridge.SNAPSHOT_REQUEST)
            expect('A0>')
            if mode == 'reboot':
                os.write(fd, b'VTREBOOT\r')
            else:
                if mode != 'idle': os.write(fd, mode.encode() + b'\r')
                time.sleep(.15)
                process.terminate()  # must stop even without guest I/O
            until = time.monotonic() + 5
            while process.poll() is None and time.monotonic() < until:
                pump()
            stdout, stderr = process.communicate(timeout=1)
            assert process.returncode == (75 if mode == 'reboot' else 0), (stdout, stderr)
            assert (area / '7XTGVTRM.BUF').exists(), 'pending history was not saved'
        finally:
            if fd is not None: smoke.bridge.close_serial(fd, saved)
            if process.poll() is None:
                process.kill(); process.wait()

for mode in ('idle', 'VTLOOP', 'VTFLOOD', 'reboot'): scenario(mode)
print('PTY boot, CARDMARK unsupported, reconnect, idle/compute/output stop and reboot: PASS')
