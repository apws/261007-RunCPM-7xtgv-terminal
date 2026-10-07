#!/usr/bin/env python3
"""Check the complete working cpmcard; optionally export a NEW offline copy.

Historical command name retained. No manifest or content filtering: drive
files, INI/BUF, backups, databases and host-side snapshots are all included.
"""
import argparse
from pathlib import Path
import shutil

from card_paths import REPO


def card_entries(root):
    """Enumerate the entire tree, rejecting links/special files rather than following them."""
    if not any((root / letter).is_dir() for letter in 'ABCDEFGHIJKLMNOP'):
        raise ValueError('No A-P drive directories found')
    directories, files = [], []
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        if path.is_symlink():
            raise ValueError(f'Symlink in card: {relative}')
        if path.is_dir():
            directories.append(relative)
        elif path.is_file():
            files.append(relative)
        else:
            raise ValueError(f'Unsupported filesystem object: {relative}')
    return directories, files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=REPO / 'cpmcard')
    parser.add_argument('--export', type=Path, help='New disposable card directory; refuses existing paths')
    args = parser.parse_args()
    try:
        root = args.root.resolve(strict=True)
        directories, files = card_entries(root)
        if args.export:
            target = args.export.expanduser().absolute()
            if target.resolve().is_relative_to(root):
                raise ValueError('Export must be outside the live source card')
            target.mkdir(parents=True, exist_ok=False)
            for relative in directories:
                (target / relative).mkdir(parents=True, exist_ok=True)
            for relative in files:
                shutil.copy2(root / relative, target / relative)
            print(f'Exported complete card: {len(files)} files to {target}')
        else:
            print(f'Complete card: {len(files)} files; no content exclusions; source unchanged')
    except (OSError, ValueError) as error:
        parser.exit(1, f'Card: {error}\n')


if __name__ == '__main__':
    main()
