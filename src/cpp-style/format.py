#!/usr/bin/env python3
"""Format only listed project C/C++; preserve directives and initializer tables."""
import argparse
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
VERSION = '23.1.1'


def opaque_mask(source):
    """Hide comments/literals so braces inside them cannot delimit a table."""
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"|(?:u8|u|U|L)?\'(?:\\[\s\S]|[^\'\\])*\''
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), source)


def protected(source):
    """Keep preprocessor logical lines and = {...} initializer bodies verbatim."""
    spans = []
    offset = 0
    start = None
    for line in source.splitlines(keepends=True):
        if start is None and re.match(r'\s*#', line):
            start = offset
        offset += len(line)
        if start is not None and not line.rstrip('\r\n').endswith('\\'):
            spans.append((start, offset))
            start = None
    mask = opaque_mask(source)
    for m in re.finditer(r'=\s*\{', mask):
        begin = mask.index('{', m.start())
        if any(a <= begin < b for a, b in spans):
            continue
        depth = 1
        end = begin + 1
        while depth and end < len(mask):
            depth += (mask[end] == '{') - (mask[end] == '}')
            end += 1
        if depth:
            raise ValueError('Unbalanced initializer')
        spans.append((begin, end))
    # Outer initializer wins over nested = {...} expressions.
    result = []
    for a, b in sorted(spans):
        if not result or a >= result[-1][1]:
            result.append((a, b))
    return result


def format_source(path):
    source = path.read_text()
    result = subprocess.run(['clang-format', '--style=file:' + str(HERE / '.clang-format'),
                             '--assume-filename=' + str(path)], input=source,
                            text=True, capture_output=True, check=True).stdout
    old, new = protected(source), protected(result)
    if len(old) != len(new):
        raise ValueError(f'Protected-region mismatch: {path}')
    for (a, b), (c, d) in reversed(list(zip(old, new))):
        result = result[:c] + source[a:b] + result[d:]
    return source, result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--write', action='store_true')
    mode.add_argument('--list', action='store_true')
    args = parser.parse_args()
    files = [line for line in (HERE / 'files.txt').read_text().splitlines()
             if line and not line.startswith('#')]
    if args.list:
        print('\n'.join(files))
        return 0
    version = subprocess.check_output(['clang-format', '--version'], text=True).strip()
    if not re.search(r'\bversion ' + re.escape(VERSION) + r'\b', version):
        parser.error(f'Requires clang-format {VERSION}; found {version}')
    changed = []
    # Compute every result first; failures leave the entire tree untouched.
    pending = []
    for name in files:
        path = ROOT / name
        if path.resolve().is_relative_to(ROOT) and path.suffix in ('.h', '.hpp', '.c', '.cpp'):
            old, new = format_source(path)
        else:
            raise ValueError(f'Invalid scope entry: {name}')
        if old != new:
            changed.append(name)
            pending.append((path, new))
    if args.write:
        for path, text in pending:
            path.write_text(text)
    elif changed:
        print('\n'.join(changed))
    print(f'{len(files)} files checked; {len(changed)} ' + ('formatted.' if args.write else 'need formatting.'))
    return int(bool(changed) and args.check)


if __name__ == '__main__':
    sys.exit(main())
