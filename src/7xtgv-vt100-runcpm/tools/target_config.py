"""Shared host target data and serial/tool selection (no device access)."""
import argparse
import glob
import json
import os
from pathlib import Path
import re
import shutil

TARGETS = Path(__file__).resolve().parent / 'targets'


def load_target(name=None, directory=None):
    name = name or os.environ.get('XTGV_TARGET')
    if not name or not re.fullmatch(r'[a-z0-9][a-z0-9-]*', name):
        raise RuntimeError('Set XTGV_TARGET or pass --target with a canonical target name')
    path = (directory or TARGETS) / f'{name}.json'
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise RuntimeError(f'Cannot load target {name}: {error}') from error
    if not isinstance(data, dict) or not isinstance(data.get('profile'), str) or not isinstance(data.get('method'), str):
        raise RuntimeError(f'Target {name} needs string profile and method fields')
    if 'port' in data and not isinstance(data['port'], str):
        raise RuntimeError(f'Target {name}: port must be a string')
    patterns = data.get('port_globs', [])
    if not isinstance(patterns, list) or any(not isinstance(p, str) for p in patterns):
        raise RuntimeError(f'Target {name}: port_globs must be a list of strings')
    if 'display_id' in data and (not isinstance(data['display_id'], str) or
                                 not re.fullmatch(r'[A-Za-z0-9_]{1,8}', data['display_id'])):
        raise RuntimeError(f'Target {name}: display_id must be 1..8 ASCII letters, digits or underscores')
    return name, data


def target_label(name, data):
    short = data.get('display_id')
    return f'{short} ({name})' if short else name


def resolve_port(port=None, target=None, definition=None):
    # Explicit port wins, then host environment, then selected target data.
    if port or os.environ.get('XTGV_PORT'):
        return port or os.environ['XTGV_PORT']
    if definition is None:
        target, definition = load_target(target)
    matches = {}
    for pattern in definition.get('port_globs', []):
        for candidate in sorted(glob.glob(pattern)):
            matches.setdefault(os.path.realpath(candidate), candidate)
    if len(matches) > 1:
        raise RuntimeError(f'Multiple serial ports match {target}; specify --port or XTGV_PORT')
    if matches:
        return next(iter(matches.values()))
    if definition.get('port'):
        return definition['port']
    raise RuntimeError(f'No serial port found for {target}; specify --port or XTGV_PORT')


def arduino_cli():
    configured = os.environ.get('ARDUINO_CLI')
    found = shutil.which(configured or 'arduino-cli')
    if found:
        return found
    if not configured:
        # Preserve common user-local installation, without a particular username.
        local = Path.home() / '.local' / 'bin' / 'arduino-cli'
        if local.is_file() and os.access(local, os.X_OK):
            return str(local)
    raise RuntimeError('Cannot find arduino-cli; set ARDUINO_CLI or add it to PATH')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target')
    parser.add_argument('--port', help='explicit port override')
    parser.add_argument('--print-port', action='store_true')
    args = parser.parse_args()
    try:
        if args.print_port:
            print(resolve_port(args.port, args.target))
        else:
            name, definition = load_target(args.target)
            print(target_label(name, definition))
    except RuntimeError as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
