#!/usr/bin/env python3
"""Interactive build/upload/mirror workflow behind per-target shell launchers."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import re
from datetime import datetime

from target_config import load_target, resolve_port, NoSerialPort, arduino_cli
import bootsel_upload

TOOLS = Path(__file__).resolve().parent
PROJECT = TOOLS.parent


class ReportedFailure(RuntimeError):
    """Short error has already been shown; do not repeat command/path details."""


def status(text):
    if sys.stdout.isatty() and not os.environ.get('NO_COLOR'):
        text = '\033[32m' + text + '\033[0m'
    print(text, flush=True)


def command(args, **kwargs):
    display = kwargs.pop('display', True)
    if display and not kwargs.get('stdout'):
        print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def failure_summary(text):
    """Return one actionable diagnostic, never compiler/uploader transcripts."""
    lines = text.splitlines()
    blocked = next((line for line in reversed(lines)
                    if line.startswith('7xtgv reflash blocked:')), None)
    if blocked:
        return 'Flash NOT performed: ' + blocked.partition(':')[2].strip()
    for line in reversed(lines):
        if re.search(r'\berror\b|\bfatal\b|\bfailed\b', line, re.IGNORECASE):
            return line[:240]
    return 'Tool exited unsuccessfully; see the detailed log.'


from target_mirror import select_mirror
from target_config import OPTIMIZATION_LEVELS, optimization_level


def ready_serial(explicit, target, definition):
    while True:
        try:
            port = resolve_port(explicit, target, definition)
            if Path(port).exists():
                return port
        except NoSerialPort:
            pass
        bootsel_upload.confirm_connection(definition.get('display_id', target))


def check_prerequisites(build_only, definition):
    missing = []
    if build_only or definition['method'] == 'arduino-cli':
        try:
            arduino_cli()
        except RuntimeError:
            missing.append('arduino-cli (or configured executable inaccessible)')
    if not build_only:
        for tool in ('tmux', *(['findmnt', 'udisksctl']
                             if definition['method'] == 'bootsel-uf2' else [])):
            if not shutil.which(tool):
                missing.append(tool)
    if missing:
        raise RuntimeError('Missing prerequisites: ' + ', '.join(missing) +
                           '. Run tools/setup.sh; nothing installed automatically.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True)
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument('--build-only', action='store_true')
    actions.add_argument('--upload-only', action='store_true')
    parser.add_argument('--port', help='Explicit serial device; otherwise target discovery')
    parser.add_argument('--chip-id', help='Explicit physical RP2350 chip ID for BOOTSEL')
    parser.add_argument('--allow-unidentified-target',
                        help='One-shot ESP recovery; must equal --target; mismatches still block')
    parser.add_argument('--mirror', choices=('auto', 'konsole', 'kitty', 'terminal', 'none'),
                        default='auto')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--verbose', action='store_true', help='Show full compiler/uploader output')
    parser.add_argument('-O', '--optimization', type=optimization_level, choices=OPTIMIZATION_LEVELS)
    args = parser.parse_args()
    if args.optimization and args.upload_only:
        parser.error('--optimization is a rebuild option, not a reflash option')
    _, definition = load_target(args.target)
    if args.allow_unidentified_target and args.allow_unidentified_target != args.target:
        parser.error('Recovery target must equal --target')
    if args.chip_id and (len(args.chip_id) != 16 or
                        any(c not in '0123456789abcdefABCDEF' for c in args.chip_id)):
        parser.error('--chip-id must contain exactly 16 hexadecimal digits')
    env = dict(os.environ, XTGV_TARGET=args.target)
    # Avoid a stale host target/port inherited from another module.
    env.pop('XTGV_PORT', None)
    os.environ.pop('XTGV_PORT', None)
    env['XTGV_REFLASH_COMMAND'] = str(TOOLS / 'reflash.py')
    base = [sys.executable, str(TOOLS / 'reflash.py'), '--target', args.target]
    build_options = ['--optimization', args.optimization] if args.optimization else []
    if args.dry_run:
        print('Target:', args.target, 'upload method:', definition['method'])
        if not args.upload_only:
            print('Build:', ' '.join(base + ['--build-only'] + build_options))
        if not args.build_only:
            print('Then stop shared mirror, validate board, upload staged new/current, '
                  'and restore mirror. No device access in dry-run.')
        return 0
    check_prerequisites(args.build_only, definition)
    target_dir = PROJECT.parents[1] / 'build' / PROJECT.name / args.target
    action = 'rebuild' if args.build_only else 'reflash'
    log_dir = TOOLS / 'targets' / f'{definition.get("display_id", args.target)} -- {args.target}'
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / f'{action}.log'
    log_path.write_text('Run: ' + datetime.now().strftime('%y%m%d-%H%M%S') + '\n')
    status(f'Module: {definition.get("display_id", args.target)} ({args.target})')
    if args.optimization:
        status('Optimization: ' + ('platform default' if args.optimization == 'default' else '-' + args.optimization))
    def section(name):
        with log_path.open('a') as log:
            log.write('\n\n*** ' + name + ' ***\n')
    def step(label, argv, name):
        status(label + ' ...')
        section(name)
        with log_path.open('a') as log:
            log.write('+ ' + ' '.join(map(str, argv)) + '\n')
            log.flush()
            try:
                if args.verbose:
                    command(argv, env=env)
                else:
                    command(argv, env=env, stdout=log, stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError:
                print(f'FAILED: {label}. See {log_path.name}', file=sys.stderr)
                if not args.verbose:
                    summary = failure_summary(log_path.read_text(errors='replace'))
                    print(summary, file=sys.stderr)
                    if re.search(r'platform.*not installed|library.*not found|No such file or directory',
                                 log_path.read_text(errors='replace'), re.IGNORECASE):
                        print('If a required tool/core/library is missing, run tools/setup.sh '
                              'for your OS; source-code errors need a separate fix.',
                              file=sys.stderr)
                    if summary.startswith('Flash NOT performed:'):
                        print('Existing firmware remains. RESET the module, then retry '
                              'with --upload-only. Unidentified-device recovery must be '
                              'explicitly confirmed.', file=sys.stderr)
                raise ReportedFailure(f'{label} failed') from None
        status(label + ': OK')
    if not args.upload_only:
        step('Building fresh firmware', base + ['--build-only'] + build_options, 'BUILD')
    image_dir = target_dir / ('new' if (target_dir / 'new').is_dir() else 'current')
    section('IMAGE')
    with log_path.open('a') as log:
        log.write(f'Firmware directory: {image_dir}\n')
    image = image_dir / (PROJECT.name + ('.ino.uf2' if definition['method'] == 'bootsel-uf2'
                                       else '.ino.bin'))
    if image.is_file():
        stamps = set(re.findall(rb'(?<![0-9])[0-9]{6}-[0-9]{4}(?![0-9])', image.read_bytes()))
        with log_path.open('a') as log:
            log.write(f'Image: {image}\n')
            if len(stamps) == 1:
                stamp = next(iter(stamps)).decode()
                log.write('Embedded build: ' + stamp + '\n')
                status('Embedded build: ' + stamp)
    if args.build_only:
        return 0
    if not shutil.which('tmux'):
        raise RuntimeError('Install tmux before the upload/mirror workflow')
    if definition['method'] not in ('arduino-cli', 'bootsel-uf2'):
        raise RuntimeError('Unsupported upload method')
    # Resolve before disturbing a working mirror where possible.
    port = (ready_serial(args.port, args.target, definition)
            if definition['method'] == 'arduino-cli' else args.port)
    mirror = select_mirror(args.mirror)
    subprocess.run(['tmux', 'kill-session', '-t', '7xtgv-cpm'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    with log_path.open('a') as log:
        log.write('Shared mirror stopped; upload will restart CP/M and lose app state.\n')
    try:
        if definition['method'] == 'arduino-cli':
            extra = ['--port', port]
            if args.allow_unidentified_target:
                extra += ['--allow-unidentified-target', args.allow_unidentified_target]
            step('Uploading firmware', base + extra, 'FLASH')
            if args.target == 'esp32c6-lcd-1-47':
                with log_path.open('a') as log:
                    log.write('C6 uses serial upload, not BOOTSEL. If uploader reset leaves '
                              'the panel blank, press hardware RESET.\n')
        else:
            images = list(image_dir.glob('*.uf2'))
            if len(images) != 1:
                raise RuntimeError('Need one staged target UF2; rebuild this target first')
            section('FLASH')
            with log_path.open('a') as log:
                chip = bootsel_upload.upload(args.target, images[0], args.chip_id,
                                             log=log, module=definition.get('display_id'),
                                             progress=status)
            status('Uploading RP2350 firmware: OK')
            # The same physical chip must reappear, never an arbitrary ttyACM.
            port = f'/dev/serial/by-id/usb-Generic_RP2350_{chip}-if00'
    finally:
        if mirror != 'none':
            if port is None:
                print('Mirror not restored: no confirmed serial port; reset and relaunch '
                      'kon.sh with the intended target.', file=sys.stderr)
            else:
                deadline = time.monotonic() + 12
                while not Path(port).exists() and time.monotonic() < deadline:
                    time.sleep(0.2)
                if not Path(port).exists():
                    print(f'Mirror not restored: {port} did not reappear; reset and '
                          'relaunch with --target/--port.', file=sys.stderr)
                else:
                    launcher = {'konsole': 'kon.sh', 'kitty': 'kit.sh',
                                'terminal': 'cpm-session.sh'}[mirror]
                    section('MIRROR')
                    with log_path.open('a') as log:
                        log.write(f'Launcher: {mirror}; port: {port}\n')
                    status('Launching mirror ...')
                    command([TOOLS / launcher, '--port', port, '--target', args.target],
                            env=env, display=False)
                    with log_path.open('a') as log:
                        log.write('Mirror launcher finished; verify boot Build or VTINFO.\n')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except ReportedFailure:
        sys.exit(1)
    except (RuntimeError, OSError, subprocess.CalledProcessError, EOFError,
            KeyboardInterrupt) as exc:
        print(f'7xtgv workflow stopped: {exc}', file=sys.stderr)
        sys.exit(1)
