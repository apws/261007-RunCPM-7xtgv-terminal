"""Launch the target's visible terminal mirror without building or uploading."""
import argparse
import os
import re
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


def mirror_command(target, port, mirror='auto', desktop_session=None):
    """One launcher selection for hardware and explicitly isolated desktop PTYs."""
    if not shutil.which('tmux'):
        raise RuntimeError('Missing tmux. Run tools/setup.sh.')
    selected = select_mirror(mirror)
    if selected not in ('konsole', 'kitty', 'terminal'):
        raise RuntimeError('Unknown mirror: ' + selected)
    if selected != 'terminal' and not shutil.which(selected):
        raise RuntimeError('Missing mirror terminal: ' + selected)
    launcher = {'konsole': 'kon.sh', 'kitty': 'kit.sh',
                'terminal': 'cpm-session.sh'}[selected]
    argv = [launcher, '--port', port, '--target', target]
    env = dict(os.environ, XTGV_TARGET=target, XTGV_PORT=port)
    if desktop_session is not None:
        if not port or not os.path.realpath(port).startswith('/dev/pts/'):
            raise RuntimeError('Desktop mirror requires an explicit /dev/pts/ endpoint')
        if not re.fullmatch(r'7xtgv-desktop-[A-Za-z0-9_-]+', desktop_session):
            raise RuntimeError('Desktop session must start with 7xtgv-desktop-')
        argv.insert(1, '--desktop-pty') # Avoid physical serial permission handling.
        env.update(XTGV_SESSION=desktop_session, XTGV_DESKTOP_TRANSPORT='pty')
        env.pop('XTGV_REFLASH_COMMAND', None)
    else:
        env.pop('XTGV_DESKTOP_TRANSPORT', None)
    return str(TOOLS / launcher), argv, env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True)
    parser.add_argument('--port', help='Explicit serial device; otherwise target discovery')
    parser.add_argument('--desktop-pty', action='store_true')
    parser.add_argument('--session', help='Required isolated 7xtgv-desktop-* session for desktop')
    parser.add_argument('--mirror', choices=('auto', 'konsole', 'kitty', 'terminal'),
                        default='auto')
    args = parser.parse_args()
    try:
        _, definition = load_target(args.target)
        if args.desktop_pty and (not args.port or not args.session):
            raise RuntimeError('--desktop-pty requires --port and --session')
        if args.session and not args.desktop_pty:
            raise RuntimeError('--session is only for --desktop-pty')
        port = args.port if args.desktop_pty else resolve_port(args.port, args.target, definition)
        executable, argv, env = mirror_command(args.target, port, args.mirror, args.session)
        os.execve(executable, argv, env)
    except (RuntimeError, OSError) as error:
        parser.exit(1, f'Mirror not launched: {error}\n')


if __name__ == '__main__':
    main()
