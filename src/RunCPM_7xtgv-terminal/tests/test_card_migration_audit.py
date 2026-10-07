"""Read-only migration audit regression, synthetic cards only."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

TOOL = Path(__file__).resolve().parents[1] / 'tools/audit_card_migration.py'
spec = importlib.util.spec_from_file_location('audit', TOOL)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)

with tempfile.TemporaryDirectory(prefix='xtgv-migration-audit-') as directory:
    root = Path(directory)
    left, right = root / 'drive', root / 'card'
    source, card = left / 'AA - BASIC', right / 'A/A'
    source.mkdir(parents=True)
    card.mkdir(parents=True)
    (source / 'SAME.COM').write_bytes(b'\xc3\x00\x01')
    (card / 'SAME.COM').write_bytes(b'\xc3\x00\x01')
    (source / 'TEXT.BAS').write_bytes(b'10 END\r\n\x1a')
    (card / 'TEXT.BAS').write_bytes(b'10 END\r\n\x1a' + b'\x00' * 100)
    (source / 'ONLY.ASM').write_bytes(b'END\r\n\x1a')
    (card / 'USER.COM').write_bytes(b'\xc9')
    (card / 'text.bas').write_bytes(b'collision')
    (left / 'layout.json').write_text('{}')
    (card / 'LINK.COM').symlink_to(source / 'SAME.COM')
    before = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in root.rglob('*')
              if p.is_file() and not p.is_symlink()}
    a, extra_a = audit.inventory(left, True)
    b, extra_b = audit.inventory(right, False)
    rows = {r['card_path']: r for r in audit.compare(a, b)}
    assert rows['A/A/SAME.COM']['status'] == 'identical'
    assert rows['A/A/TEXT.BAS']['status'] == 'case_collision'
    assert rows['A/A/ONLY.ASM']['status'] == 'cpmdrive_only'
    assert rows['A/A/USER.COM']['status'] == 'card_only'
    assert extra_a[0]['path'] == 'layout.json'
    assert any(x['reason'].startswith('symlink') for x in extra_b)
    report = root / 'audit.json'
    subprocess.run([sys.executable, str(TOOL), str(left), str(right), '--output', str(report)], check=True)
    assert json.loads(report.read_text())['summary']['case_collision'] == 1
    refused = subprocess.run([sys.executable, str(TOOL), str(left), str(right),
                              '--output', str(card / 'REPORT.JSON')], capture_output=True)
    assert refused.returncode != 0 and not (card / 'REPORT.JSON').exists()
    assert all((p.read_bytes(), p.stat().st_mtime_ns) == value for p, value in before.items())
print('Card migration audit: mapping, hashes, collisions, symlinks, no input writes PASS')
