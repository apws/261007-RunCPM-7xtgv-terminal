"""Canonical host card layout. No descriptive-folder or old-mirror fallback."""
import os
from pathlib import Path
import re

REPO = Path(__file__).resolve().parents[3]


def card_root():
    """Working card for host fixtures; mounted cards require explicit selection."""
    return Path(os.environ.get('XTGV_CARD_ROOT', str(REPO / 'cpmcard'))).expanduser().absolute()


def transfer_root():
    return Path(os.environ.get('XTGV_TRANSFER_ROOT',
        os.environ.get('XTGV_XMODEM_ROOT', str(card_root())))).expanduser().absolute()


def du_directory(root, value):
    match = re.fullmatch(r'([A-P])([0-9A-F]|1[0-5]):?', value.strip().upper())
    if not match:
        raise ValueError('Use DU such as A0:, A6:, AF: (or A15:).')
    user = int(match[2], 10 if len(match[2]) == 2 else 16)
    drive, area = match[1], format(user, 'X')
    root = Path(root).absolute()
    directory = root / drive / area
    if (root / drive).is_symlink() or directory.is_symlink():
        raise ValueError('DU directories must not be symlinks.')
    if not directory.is_dir():
        raise ValueError(f'No local folder for {drive}{area}:; expected {directory}')
    return drive + area + ':', directory


def require_files(*paths):
    missing = [str(p) for p in paths if not Path(p).is_file()]
    if missing:
        raise SystemExit('Missing card fixture(s): ' + ', '.join(missing) +
            '. Provision the required files in cpmcard (see cpmcard/README.adoc), '
            'or select a prepared copy with XTGV_CARD_ROOT.')


if __name__ == '__main__':
    print(card_root())
