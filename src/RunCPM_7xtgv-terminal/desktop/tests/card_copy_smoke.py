"""Explicit source-card check: run ONLY on a new disposable copy, retain evidence."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=Path)
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='card-copy-', dir=args.output.resolve()))
card = work / 'card'
shutil.copytree(args.source, card, symlinks=True)
# Preserve source INI/history and all guest programs; writes affect only copy.
keyboard = work / 'input.bin'
keyboard.write_bytes(b'DIR\r')
picture = work / 'panel.ppm'
result = subprocess.run([str(args.executable.resolve()), '--card-root', str(card),
                         '--input', str(keyboard), '--run-for-ms', '2200',
                         '--snapshot', str(picture)], capture_output=True, text=True, timeout=15)
(work / 'run.log').write_text(result.stdout + result.stderr)
assert result.returncode == 0, (work, result.stdout, result.stderr)
assert picture.stat().st_size > 1000
print(f'Copied-card boot/shutdown PASS: {work}')
