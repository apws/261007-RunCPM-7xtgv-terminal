#!/usr/bin/env python3
"""Build, test or launch a real-board desktop configuration; never flash hardware."""
import argparse
import codecs
import json
import os
from pathlib import Path
import selectors
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
SKETCH = HERE.parent
REPO = SKETCH.parents[1]
sys.path.insert(0, str(SKETCH / 'tools'))
from target_mirror import mirror_command
from target_config import OPTIMIZATION_LEVELS, optimization_level
from desktop_card_lock import card_lock
TARGETS = {p.stem: json.loads(p.read_text()) for p in (SKETCH / 'tools/targets').glob('*.json')}

def target_name(name):
    for key, value in TARGETS.items():
        if name.lower() in (key.lower(), value['display_id'].lower()):
            return key
    raise argparse.ArgumentTypeError(f'Unknown board {name}; use ' + ', '.join(v['display_id'] for v in TARGETS.values()))

def build(args, target):
    directory = REPO / 'build' / SKETCH.name / 'desktop' / target
    directory.mkdir(parents=True, exist_ok=True)
    library = args.gfx.resolve()
    optimization = args.optimization if args.optimization not in (None, 'default') else 'O2'
    commands = [
        ['cmake', '-S', str(HERE), '-B', str(directory), f'-DXTGV_TARGET={target}',
         f'-DXTGV_GFX_SOURCE={library}', '-DCMAKE_BUILD_TYPE=Debug',
         f'-DXTGV_OPTIMIZATION={optimization}'],
        ['cmake', '--build', str(directory), '-j2'],
    ]
    if args.quiet: print('Building desktop ...', flush=True)
    print('Optimization: -' + optimization, flush=True)
    with (args.log or directory / 'build.log').open('w') as log:
        for section, command in zip(('CONFIGURE', 'BUILD'), commands):
            log.write('\n\n*** ' + section + ' ***\n' + repr(command) + '\n'); log.flush()
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    print('Building desktop: OK' if args.quiet else f"Built {TARGETS[target]['display_id']}: {directory}", flush=True)
    return directory

def tests(directory):
    subprocess.run(['ctest', '--test-dir', str(directory), '--output-on-failure'], check=True)

def wait_for_desktop(child, mirror, report):
    """Drain output while watching the owning mirror, even when the guest is quiet."""
    mirror_closed = False
    decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')
    with selectors.DefaultSelector() as ready:
        ready.register(child.stdout, selectors.EVENT_READ)
        while child.poll() is None or ready.get_map():
            if not mirror_closed and mirror is not None and mirror.poll() is not None:
                mirror_closed = True
                if child.poll() is None:
                    # SIGTERM follows the native cooperative storage/worker shutdown.
                    child.terminate()
            for key, _ in ready.select(timeout=0.1):
                chunk = os.read(key.fd, 4096)
                if chunk:
                    report(decoder.decode(chunk))
                else:
                    ready.unregister(key.fileobj)
        tail = decoder.decode(b'', final=True)
        if tail:
            report(tail)
    code = child.wait()
    # An explicit window close wins over a simultaneous guest REBOOT.
    return 0 if mirror_closed and code == 75 else code

def run(args, target, directory):
    def report(line):
        if args.run_log:
            args.run_log.write(line); args.run_log.flush()
        if not args.quiet:
            print(line, end='', flush=True)
        elif line.startswith('Desktop ') and ' | ' in line:
            print('Embedded build: ' + line.split(' | ')[1], flush=True)

    executable = directory / 'xtgv_desktop'
    if not executable.exists():
        raise RuntimeError('Build first with: desktop.py build --target ' + target)
    root = args.card_root.resolve(strict=True)
    command = [str(executable), '--card-root', str(root)]
    if args.window_scale != 1: command += ['--window-scale', str(args.window_scale)]
    if args.headless: command += ['--headless']
    if args.input: command += ['--input', str(args.input.resolve())]
    if args.snapshot: command += ['--snapshot', str(args.snapshot.resolve())]
    if args.run_for_ms: command += ['--run-for-ms', str(args.run_for_ms)]
    if not args.no_mirror:
        # Validate prerequisites before starting the guest. Real endpoint is
        # supplied below; this check does not open a port or launch a terminal.
        mirror_command(target, '/dev/pts/0', args.mirror, '7xtgv-desktop-check')
        command += ['--pty']
    while True:
        if args.run_log:
            args.run_log.write('\n\n*** DESKTOP ***\n' + repr(command) + '\n'); args.run_log.flush()
        if args.quiet: print('Launching desktop ...', flush=True)
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=args.run_log, text=True)
        mirror = None
        session = f'7xtgv-desktop-{child.pid}'
        try:
            if not args.no_mirror:
                with selectors.DefaultSelector() as ready:
                    ready.register(child.stdout, selectors.EVENT_READ)
                    endpoint = ''
                    pending = b''
                    deadline = time.monotonic() + 10
                    while not endpoint and time.monotonic() < deadline:
                        if not ready.select(timeout=max(0, deadline - time.monotonic())): break
                        chunk = os.read(child.stdout.fileno(), 4096)
                        if not chunk: break
                        pending += chunk
                        while b'\n' in pending:
                            line, pending = pending.split(b'\n', 1)
                            report(line.decode(errors='replace') + '\n')
                            if line.startswith(b'PTY='): endpoint = line[4:].decode()
                    if pending: report(pending.decode(errors='replace'))
                    if not endpoint:
                        raise RuntimeError('Desktop did not create its mirror endpoint')
                mirror_executable, argv, env = mirror_command(target, endpoint, args.mirror, session)
                if args.run_log:
                    args.run_log.write('\n\n*** MIRROR ***\n' + repr(argv) + '\n'); args.run_log.flush()
                if args.quiet: print('Launching mirror ...', flush=True)
                # Current-terminal mode needs its real tty; GUI launcher chatter
                # belongs in the log, not in the simple command's status output.
                gui_log = args.run_log if not mirror_executable.endswith('/cpm-session.sh') else None
                mirror = subprocess.Popen([mirror_executable, *argv[1:]], env=env, stdout=gui_log, stderr=gui_log)
            code = wait_for_desktop(child, mirror, report)
        except KeyboardInterrupt:
            child.terminate(); code = child.wait()
        finally:
            if child.poll() is None:
                child.terminate(); child.wait()
            child.stdout.close()
            if mirror is not None:
                subprocess.run(['tmux', 'kill-session', '-t', session], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                try:
                    mirror.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    mirror.terminate()
                    try:
                        mirror.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        mirror.kill(); mirror.wait()
        if code != 75:
            if args.quiet: print('Desktop stopped.' if code == 0 else 'FAILED: desktop exited; see ' + args.log.name, flush=True)
            return code
        print('Desktop reboot: starting a fresh process.', flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'test', 'matrix', 'run'])
    parser.add_argument('--target', type=target_name, default='jc8012p4a1c')
    parser.add_argument('--gfx', type=Path, default=Path.home() / 'Arduino/libraries/GFX_Library_for_Arduino')
    parser.add_argument('--window-scale', type=int, choices=range(1, 10), default=1,
                        help='Fixed SDL window integer scale (1..9); guest geometry is unchanged')
    parser.add_argument('--card-root', type=Path)
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--no-mirror', action='store_true')
    parser.add_argument('--mirror', choices=['auto', 'konsole', 'kitty', 'terminal'], default='auto')
    parser.add_argument('--input', type=Path)
    parser.add_argument('--snapshot', type=Path)
    parser.add_argument('--run-for-ms', type=int, default=0)
    parser.add_argument('--quiet', action='store_true')
    parser.add_argument('--log', type=Path)
    parser.add_argument('-O', '--optimization', type=optimization_level, choices=OPTIMIZATION_LEVELS)
    args = parser.parse_args()
    if args.window_scale != 1 and args.action != 'run':
        parser.error('--window-scale is a run option; it does not change the built panel')
    if args.optimization and args.action == 'run':
        parser.error('--optimization is a build/test/matrix option; run uses the existing binary')
    if args.action == 'run' and not args.card_root:
        parser.error('run requires --card-root (copied cpmcard or mounted reader directory)')
    if args.quiet and not args.log:
        parser.error('--quiet requires --log')
    args.run_log = None
    try:
        if args.action == 'matrix':
            for target in sorted(TARGETS): tests(build(args, target))
        elif args.action in ('build', 'test'):
            directory = build(args, args.target)
            if args.action == 'test': tests(directory)
        else:
            if args.log: args.run_log = args.log.open('w')
            with card_lock(args.card_root):
                return run(args, args.target, REPO / 'build' / SKETCH.name / 'desktop' / args.target)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        if args.run_log: args.run_log.write('\nERROR: ' + str(error) + '\n')
        reason = ('Build/launcher command failed' if isinstance(error, subprocess.SubprocessError)
                  else error.strerror if isinstance(error, OSError) else str(error))
        log_hint = args.log.name if args.log else f'build/{SKETCH.name}/desktop/<target>/build.log'
        print(f'FAILED: {reason}. See {log_hint}', file=sys.stderr)
        return 1
    finally:
        if args.run_log: args.run_log.close()
    return 0

if __name__ == '__main__':
    sys.exit(main())
