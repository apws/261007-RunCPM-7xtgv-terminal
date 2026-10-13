#!/usr/bin/env python3
"""Quiet web shortcuts with adjacent logs; no install, flash or deployment."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import traceback

from build_assets import BUILD, HERE

sys.path.insert(0, str(HERE.parent / 'desktop'))
from build_progress import run_build


def logged(command, log, **kwargs):
    log.write('\n*** COMMAND ***\n' + repr(command) + '\n'); log.flush()
    return subprocess.run(command, stderr=log, check=True, **kwargs)


def rebuild(log):
    sdk = BUILD / '_deps/emsdk/emsdk_env.sh'
    if not sdk.is_file():
        raise RuntimeError('Run tools-web/setup.sh first')
    print('Configuring browser ...', flush=True)
    # Source the SDK in a child shell. NUL-delimited environment values stay
    # intact (including spaces/newlines), and never appear in the log.
    result = logged(['bash', '-c', 'source "$1" >/dev/null && exec env -0',
                     'web-sdk', str(sdk)], log, stdout=subprocess.PIPE)
    environment = dict(os.fsdecode(item).split('=', 1)
                       for item in result.stdout.split(b'\0') if item)
    directory = BUILD / 'shared'
    directory.mkdir(parents=True, exist_ok=True)
    gfx = os.environ.get('XTGV_GFX_SOURCE', str(Path.home() / 'Arduino/libraries/GFX_Library_for_Arduino'))
    logged(['emcmake', 'cmake', '-S', str(HERE.parent / 'desktop/web'),
            '-B', str(directory), '-G', 'Unix Makefiles', '-DXTGV_GFX_SOURCE=' + gfx],
           log, stdout=log, env=environment)
    command = ['cmake', '--build', str(directory), '-j', '2']
    log.write('\n*** BUILD ***\n' + repr(command) + '\n'); log.flush()
    run_build(command, log, env=environment, label='Building browser')
    print('Packaging browser ...', flush=True)
    logged([sys.executable, str(HERE / 'build_assets.py')], log, stdout=log)
    print('Browser build: OK', flush=True)


def serve(command, log):
    """Keep the host in the foreground, logging all child and browser chatter."""
    log.write('\n*** LAUNCH ***\n' + repr(command) + '\n'); log.flush()
    print('Preparing browser ...', flush=True)
    child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, encoding='utf-8', errors='replace', bufsize=1)
    try:
        for line in child.stdout:
            log.write(line); log.flush()
            if line.startswith('URL:'):
                print('Browser server: ready', flush=True)
                print(line.rstrip(), flush=True)
                print('Ctrl+C stops local hosting.', flush=True)
            elif line.startswith('Waiting for the browser'):
                print('Waiting for browser ...', flush=True)
            elif line.startswith('Running. Exit the mirror'):
                print('Browser mirror: running', flush=True)
        code = child.wait()
        if code:
            raise subprocess.CalledProcessError(code, command)
    except KeyboardInterrupt:
        log.write('\nLocal hosting interrupted.\n'); log.flush()
        if child.poll() is None:
            child.send_signal(signal.SIGINT)
            child.wait(timeout=15)
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill(); child.wait()
        child.stdout.close()
    print('Browser hosting stopped.', flush=True)


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)
    if not args or args[0] not in ('rebuild', 'local'):
        print('Usage: shortcut.py rebuild | local [browser options]', file=sys.stderr)
        return 2
    action = args.pop(0)
    path = HERE / ('desktop-' + action + '.log')
    try:
        with path.open('w', encoding='utf-8') as log:
            log.write(f'*** {time.strftime("%F %T")} WEB {action} ***\n'); log.flush()
            print('Log: ' + path.name, flush=True)
            try:
                if action == 'rebuild':
                    if args:
                        raise RuntimeError('desktop-rebuild.sh takes no arguments; set XTGV_GFX_SOURCE to override GFX')
                    rebuild(log)
                else:
                    native = bool(args and args[0] == '--native-mirror')
                    if native:
                        args.pop(0)
                    python = BUILD / '_deps/venv/bin/python' if native else Path(sys.executable)
                    if not python.is_file():
                        raise RuntimeError('Run tools-web/setup.sh first')
                    command = [str(python), '-u', str(HERE / ('launch.py' if native else 'browser.py')), *args]
                    if '--help' in args or '-h' in args:
                        return subprocess.call(command)
                    serve(command, log)
            except KeyboardInterrupt:
                log.write('\nCancelled.\n')
                print('Cancelled; see ' + path.name, file=sys.stderr)
                return 130
            except Exception as error:
                traceback.print_exc(file=log)
                reason = str(error) if isinstance(error, RuntimeError) else 'command failed'
                print(f'FAILED: {reason}. See {path.name}', file=sys.stderr)
                return error.returncode if isinstance(error, subprocess.CalledProcessError) and error.returncode > 0 else 1
    except OSError as error:
        print(f'FAILED: cannot write {path.name}: {error.strerror}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
