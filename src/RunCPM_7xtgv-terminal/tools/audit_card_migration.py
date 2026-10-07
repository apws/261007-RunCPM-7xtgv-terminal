#!/usr/bin/env python3
"""Read-only cpmdrive-to-card inventory. Never chooses winners or synchronizes.

Map descriptive DU directories (AA - label) to RunCPM paths (A/A).
Hashes are authoritative for equality; mtime order is only a review hint.
Output is JSON on stdout, optionally saved as generated evidence with --output.
"""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re


def inventory(root, descriptive):
    entries = defaultdict(list)
    other = []
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            other.append({'path': str(path.relative_to(root)), 'reason': 'symlink; not followed'})
            continue
        if not path.is_file():
            continue
        parts = path.relative_to(root).parts
        match = re.fullmatch(r'([A-P])([0-9A-F]) - .+', parts[0]) if descriptive else None
        if descriptive and match and len(parts) == 2:
            key = '/'.join((*match.groups(), parts[1].upper()))
        elif not descriptive and len(parts) == 3 and re.fullmatch('[A-P]', parts[0]) and re.fullmatch('[0-9A-F]', parts[1]):
            key = '/'.join((parts[0], parts[1], parts[2].upper()))
        else:
            other.append({'path': str(path.relative_to(root)), 'reason': 'outside flat DU files'})
            continue
        data = path.read_bytes()
        stat = path.stat()
        entry = {'path': str(path.relative_to(root)), 'size': len(data),
                 'sha256': hashlib.sha256(data).hexdigest(), 'mtime_ns': stat.st_mtime_ns,
                 'mtime_utc': datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat(),
                 'canonical_name': parts[-1] == parts[-1].upper()}
        # Only flag demonstrably ASCII text before Ctrl-Z. No extension-based
        # conversion; this hint does NOT authorize discarding binary/tail bytes.
        body, eof, tail = data.partition(b'\x1a')
        if body and all(c in (9, 10, 13) or 32 <= c < 127 for c in body):
            normalized = body.replace(b'\r\n', b'\n').rstrip(b'\n')
            entry['normalized_ascii_sha256'] = hashlib.sha256(normalized).hexdigest()
        if eof and body and all(c in (9, 10, 13) or 32 <= c < 127 for c in body):
            entry['ascii_body_sha256'] = hashlib.sha256(body).hexdigest()
            entry['tail_bytes'] = len(tail)
        entries[key].append(entry)
    return entries, other


def compare(left, right):
    rows = []
    for key in sorted(left.keys() | right.keys()):
        a, b = left.get(key, []), right.get(key, [])
        row = {'card_path': key, 'cpmdrive': a, 'cpmcard': b}
        if len(a) > 1 or len(b) > 1:
            row['status'] = 'case_collision'
        elif not a or not b:
            row['status'] = 'card_only' if not a else 'cpmdrive_only'
        elif a[0]['sha256'] == b[0]['sha256']:
            row['status'] = 'identical'
        else:
            row['status'] = 'different'
            delta = a[0]['mtime_ns'] - b[0]['mtime_ns']
            row['mtime_hint'] = 'cpmdrive_later' if delta > 0 else 'card_later' if delta < 0 else 'equal'
            row['same_ascii_body'] = bool(a[0].get('ascii_body_sha256') and
                a[0]['ascii_body_sha256'] == b[0].get('ascii_body_sha256'))
        rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cpmdrive', type=Path)
    parser.add_argument('cpmcard', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    a, b = args.cpmdrive.resolve(strict=True), args.cpmcard.resolve(strict=True)
    if not a.is_dir() or not b.is_dir() or a == b:
        parser.error('Use two distinct existing directories')
    if args.output:
        output = args.output.resolve()
        if output.is_relative_to(a) or output.is_relative_to(b):
            parser.error('Report must be outside both input trees')
    left, left_other = inventory(a, True)
    right, right_other = inventory(b, False)
    rows = compare(left, right)
    for row in rows:
        if row['status'] != 'different':
            continue
        x, y = row['cpmdrive'][0], row['cpmcard'][0]
        first, second = (a / x['path']).read_bytes(), (b / y['path']).read_bytes()
        if first.startswith(second) or second.startswith(first):
            row['byte_relation'] = 'exact-prefix-plus-tail'
        elif row['same_ascii_body']:
            row['byte_relation'] = 'same-ASCII-before-EOF'
        elif x.get('normalized_ascii_sha256') and x['normalized_ascii_sha256'] == y.get('normalized_ascii_sha256'):
            row['byte_relation'] = 'text-newlines-or-EOF'
        else:
            row['byte_relation'] = 'review-content'
    result = {'cpmdrive_root': str(a), 'cpmcard_root': str(b),
              'summary': dict(Counter(row['status'] for row in rows)),
              'cpmdrive_other': left_other, 'cpmcard_other': right_other, 'files': rows}
    report = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(report)
        print(json.dumps(result['summary'], sort_keys=True))
    else:
        print(report, end='')


if __name__ == '__main__':
    main()
