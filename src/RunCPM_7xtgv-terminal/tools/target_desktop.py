"""Simple per-board desktop shortcuts. Never discover USB serial or flash hardware."""
import argparse
import os
from pathlib import Path
import shlex
import sys

from target_config import load_target, target_label, OPTIMIZATION_LEVELS, optimization_level
from card_paths import card_root as working_card_root

TOOLS = Path(__file__).resolve().parent
SKETCH = TOOLS.parent
REPO = SKETCH.parents[1]


def card_root(action, explicit=None):
    requested = explicit
    if not requested and action == 'local':
        requested = working_card_root()
    if not requested:
        requested = os.environ.get('XTGV_DESKTOP_CARD_ROOT')
    if not requested:
        if not sys.stdin.isatty():
            raise RuntimeError('Specify --card-root PATH or XTGV_DESKTOP_CARD_ROOT for the mounted card')
        requested = input('Mounted SD/USB card directory (Ctrl+C cancels): ').strip()
        if not requested:
            raise RuntimeError('No card directory selected')
    root = Path(requested).expanduser().resolve(strict=True)
    if not (root / 'A' / '0').is_dir():
        raise RuntimeError(f'Card directory needs A/0: {root}')
    if action == 'card' and not os.path.ismount(root):
        raise RuntimeError(f'Not a mounted filesystem root: {root}; use desktop-local.sh for ordinary directories')
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('build', 'local', 'card'))
    parser.add_argument('--target', required=True)
    parser.add_argument('--window-scale', type=int, choices=range(1, 10), default=1,
                        help='Fixed SDL window integer scale (1..9); guest geometry is unchanged')
    parser.add_argument('--card-root')
    parser.add_argument('--gfx', help='GFX 1.6.4 source directory for build')
    parser.add_argument('-O', '--optimization', type=optimization_level, choices=OPTIMIZATION_LEVELS)
    parser.add_argument('--mirror', choices=('auto', 'konsole', 'kitty', 'terminal'), default='auto')
    parser.add_argument('--dry-run', action='store_true', help='Show command only; no build, launch or card writes')
    parser.add_argument('--log', type=Path, required=True, help='Detailed shortcut log')
    args = parser.parse_args()
    try:
        name, definition = load_target(args.target)
        command = [sys.executable, str(SKETCH / 'desktop/desktop.py'),
                   'build' if args.action == 'build' else 'run', '--target', name,
                   '--quiet', '--log', str(args.log.resolve())]
        print('Desktop module: ' + target_label(name, definition), flush=True)
        if args.action == 'build':
            if args.window_scale != 1: raise RuntimeError('--window-scale is only for launch')
            if args.card_root: raise RuntimeError('--card-root is only for launch')
            if args.gfx: command += ['--gfx', args.gfx]
            if args.optimization: command += ['--optimization', args.optimization]
        else:
            if args.optimization: raise RuntimeError('--optimization is only for desktop-rebuild.sh')
            if args.gfx: raise RuntimeError('--gfx is only for build')
            root = card_root(args.action, args.card_root)
            command += ['--card-root', str(root), '--mirror', args.mirror,
                        '--window-scale', str(args.window_scale)]
            print('Card: ' + ('local folder' if args.action == 'local' else 'mounted SD/USB') +
                  ' (read/write, including INI/history)', flush=True)
        if args.dry_run:
            print(shlex.join(command))
            return 0
        os.execv(sys.executable, command)
    except (OSError, RuntimeError) as error:
        parser.exit(1, f'Desktop not started: {error}\n')
    except (KeyboardInterrupt, EOFError):
        parser.exit(130, '\nDesktop launch cancelled.\n')


if __name__ == '__main__':
    sys.exit(main())
