#!/usr/bin/env python3
"""Audit a formatting-only change against a Git revision using Clang's lexer."""
import argparse
import ast
from pathlib import Path
import re
import subprocess

from format import HERE, ROOT, VERSION, protected


def tokens(source):
    # C/C++ translation phase 2 removes line splices before lexing. Exact
    # preprocessor logical lines are additionally compared by protected().
    source = source.replace('\\\n', '')
    result = subprocess.run(['clang', '-cc1', '-x', 'c++', '-dump-raw-tokens', '-'],
                            input=source, text=True, capture_output=True, check=True)
    output = []
    for line in result.stderr.splitlines():
        if not line.strip():
            continue
        if 'Loc=<' not in line:
            raise ValueError('Unexpected Clang token dump: ' + line)
        kind, value = line.split('Loc=<', 1)[0].strip().split(None, 1)
        if kind == 'comment':
            continue
        if kind == 'unknown' and ast.literal_eval(value).isspace():
            continue
        output.append((kind, value))
    return output


def original(base, name):
    return subprocess.check_output(['git', 'show', base + ':' + name], cwd=ROOT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default='HEAD', help='Revision before the formatting-only change')
    args = parser.parse_args()
    version = subprocess.check_output(['clang', '--version'], text=True)
    if not re.search(r'\bversion ' + re.escape(VERSION) + r'\b', version):
        parser.error(f'Requires Clang {VERSION} for its raw-token dump format')
    files = [s for s in (HERE / 'files.txt').read_text().splitlines() if s and not s.startswith('#')]
    for name in files:
        old = original(args.base, name).decode()
        new = (ROOT / name).read_text()
        if tokens(old) != tokens(new):
            raise ValueError('C/C++ token change: ' + name)
        if [old[a:b] for a, b in protected(old)] != [new[a:b] for a, b in protected(new)]:
            raise ValueError('Preprocessor or initializer change: ' + name)
    tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
    excluded = [p for p in tracked if Path(p).suffix.lower() in ('.h', '.hpp', '.c', '.cpp', '.ino')
                and p not in files]
    for name in excluded:
        if (ROOT / name).read_bytes() != original(args.base, name):
            raise ValueError('Excluded C/C++ file changed: ' + name)
    print(f'PASS: tokens/directives/tables unchanged in {len(files)} scoped files; '
          f'{len(excluded)} excluded C/C++ files byte-identical.')


if __name__ == '__main__':
    main()
