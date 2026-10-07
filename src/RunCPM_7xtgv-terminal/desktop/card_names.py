#!/usr/bin/env python3
"""Audit canonical uppercase CP/M drive/user names; --apply explicitly renames."""
import argparse
from pathlib import Path
import re
import sys
import uuid

NAME = re.compile(r"[A-Z0-9!#$%&'()@^_`{}~+-]{1,8}(?:\.[A-Z0-9!#$%&'()@^_`{}~+-]{1,3})?\Z")

def inventory(root):
    changes, errors = [], []
    def children(parent):
        entries = sorted(parent.iterdir())
        groups = {}
        for entry in entries:
            groups.setdefault(entry.name.upper(), []).append(entry.name)
        for names in groups.values():
            if len(names) > 1: errors.append(f'Collision in {parent}: {names}')
        return entries
    def normalize(entry):
        if entry.name != entry.name.upper():
            changes.append((entry, entry.with_name(entry.name.upper())))
    for drive in children(root):
        if not re.fullmatch('[A-Pa-p]', drive.name) or not drive.is_dir(): continue
        if drive.is_symlink(): errors.append(f'Symlink drive: {drive}'); continue
        normalize(drive)
        for user in children(drive):
            if not re.fullmatch('[0-9A-Fa-f]', user.name) or not user.is_dir(): continue
            if user.is_symlink(): errors.append(f'Symlink user: {user}'); continue
            normalize(user)
            for entry in children(user):
                if entry.is_symlink() or not entry.is_file():
                    errors.append(f'Unsupported non-file in CP/M user: {entry}'); continue
                if not NAME.fullmatch(entry.name.upper()):
                    errors.append(f'Unsupported CP/M filename: {entry}'); continue
                normalize(entry)
    return sorted(changes, key=lambda pair: len(pair[0].parts), reverse=True), errors

def apply(changes):
    for source, target in changes:
        # Existing case aliases on FAT may refer to the same file; other files
        # must never be overwritten. The full inventory is checked first.
        if target.exists() and not source.samefile(target):
            raise RuntimeError(f'Collision appeared: {target}')
        temporary = source.with_name('.xtgv-case-' + uuid.uuid4().hex)
        if temporary.exists(): raise RuntimeError(f'Temporary name exists: {temporary}')
        source.rename(temporary)
        try:
            if target.exists(): raise RuntimeError(f'Collision appeared: {target}')
            temporary.rename(target)
        except BaseException:
            temporary.rename(source)
            raise

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('card_root', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    try:
        changes, errors = inventory(args.card_root.resolve(strict=True))
        for source, target in changes: print(f'{source} -> {target.name}')
        for error in errors: print(error, file=sys.stderr)
        print(f'{len(changes)} renames; {len(errors)} problems; ' + ('APPLY requested' if args.apply else 'dry-run'))
        if errors: return 1
        if args.apply: apply(changes)
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr); return 1
    return 0

if __name__ == '__main__': sys.exit(main())
