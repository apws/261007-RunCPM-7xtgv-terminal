"""Launch the target's visible terminal mirror without building or uploading."""
import argparse
import os
from pathlib import Path
import shutil
import sys

from target_config import load_target, resolve_port

TOOLS = Path(__file__).resolve().parent


def select_mirror(mirror='auto'):
    if mirror != 'auto':
        return mirror
    if os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'):
        for candidate in ('konsole', 'kitty'):
            if shutil.which(candidate):
                return candidate
    return 'terminal'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True)
    parser.add_argument('--port', help='Explicit serial device; otherwise target discovery')
    parser.add_argument('--mirror', choices=('auto', 'konsole', 'kitty', 'terminal'),
                        default='auto')
    args = parser.parse_args()
    try:
        _, definition = load_target(args.target)
        port = resolve_port(args.port, args.target, definition)
        if not shutil.which('tmux'):
            raise RuntimeError('Missing tmux. Run tools/setup.sh.')
        mirror = select_mirror(args.mirror)
        launcher = {'konsole': 'kon.sh', 'kitty': 'kit.sh',
                    'terminal': 'cpm-session.sh'}[mirror]
        env = dict(os.environ, XTGV_TARGET=args.target, XTGV_PORT=port)
        os.execve(str(TOOLS / launcher),
                  [launcher, '--port', port, '--target', args.target], env)
    except (RuntimeError, OSError) as error:
        parser.exit(1, f'Mirror not launched: {error}\n')


if __name__ == '__main__':
    main()
