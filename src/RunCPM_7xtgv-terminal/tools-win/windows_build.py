#!/usr/bin/env python3
"""MSYS2 UCRT64 desktop build adapter; no launch, card access or hidden install."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
SKETCH = HERE.parent
REPO = SKETCH.parents[1]
sys.path.insert(0, str(SKETCH / 'desktop'))
from desktop import target_name, TARGETS
from target_config import optimization_level
from shortcut_log import shortcut_log, append_log

OUTPUT = REPO / 'build' / SKETCH.name / 'desktop-win'
GFX_ROOT = OUTPUT / '_deps' / 'GFX_Library_for_Arduino-1.6.4'
GFX_URL = 'https://downloads.arduino.cc/libraries/github.com/moononournation/GFX_Library_for_Arduino-1.6.4.zip'
GFX_SHA256 = '08d5705b79484e345fec1f3b521aa9edb61639ce3a2465badba8b0aa75f04f6f'


def require_msys():
    if sys.platform != 'cygwin' or os.environ.get('MSYSTEM') != 'UCRT64':
        raise RuntimeError('Use tools-win Windows launchers or MSYS Python in MSYS2 UCRT64.')


def gfx_valid(path):
    properties = path / 'library.properties'
    return (properties.is_file() and 'version=1.6.4' in properties.read_text().splitlines()
            and (path / 'src/Arduino_GFX.cpp').is_file())


def install_gfx(destination=GFX_ROOT):
    if destination.exists():
        if not gfx_valid(destination):
            raise RuntimeError(f'Existing GFX path is not 1.6.4; preserved: {destination}')
        print(f'GFX 1.6.4 already present: {destination}')
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='gfx-download-', dir=destination.parent) as temp:
        archive = Path(temp) / 'gfx.zip'
        print('Downloading pinned Arduino_GFX 1.6.4 ...', flush=True)
        with urllib.request.urlopen(GFX_URL, timeout=60) as response, archive.open('wb') as out:
            shutil.copyfileobj(response, out)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != GFX_SHA256:
            raise RuntimeError('Arduino_GFX archive checksum mismatch; not installed')
        with zipfile.ZipFile(archive) as package:
            # Extract only the expected library tree from the checked archive.
            prefix = 'GFX_Library_for_Arduino-1.6.4/'
            for member in package.infolist():
                if not member.filename.startswith(prefix) or '..' in Path(member.filename).parts:
                    raise RuntimeError('Unexpected Arduino_GFX archive member')
            package.extractall(temp)
        extracted = Path(temp) / prefix
        if not gfx_valid(extracted):
            raise RuntimeError('Downloaded archive is not the expected GFX library')
        extracted.rename(destination)
    print(f'Installed verified GFX 1.6.4: {destination}')


def host_path(value):
    if sys.platform == 'cygwin':
        value = subprocess.check_output(['/usr/bin/cygpath', '-u', str(value)], text=True).strip()
    return Path(value).expanduser().resolve()


def native_path(path):
    if sys.platform == 'cygwin':
        return subprocess.check_output(['/usr/bin/cygpath', '-m', str(path)], text=True).strip()
    return str(path) # Linux is allowed to preview commands, never execute them.


def build_commands(target, library, optimization, jobs):
    directory = OUTPUT / 'msys2-ucrt64' / target
    cmake = '/ucrt64/bin/cmake.exe'
    return directory, [
        [cmake, '-S', native_path(SKETCH / 'desktop'), '-B', native_path(directory),
         '-G', 'Ninja', '-DCMAKE_CXX_COMPILER=' + native_path(Path('/ucrt64/bin/g++.exe')),
         '-DCMAKE_MAKE_PROGRAM=' + native_path(Path('/ucrt64/bin/ninja.exe')),
         '-DSDL2_DIR=' + native_path(Path('/ucrt64/lib/cmake/SDL2')),
         '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DXTGV_TARGET=' + target,
         '-DXTGV_GFX_SOURCE=' + native_path(library), '-DXTGV_OPTIMIZATION=' + optimization],
        [cmake, '--build', native_path(directory), '--parallel', str(jobs)],
    ]


def stage_runtime(directory, runtime, system, objdump, environment, log):
    """Resolve PE imports from this toolchain, then copy their transitive closure.

    Windows supplies System32/API-set imports. Never borrow DLLs from PATH or
    trust stale copies left beside a previous executable.
    """
    available = {p.name.casefold(): p for p in runtime.iterdir()
                 if p.is_file() and p.suffix.casefold() == '.dll'}
    system_names = {p.name.casefold() for p in system.iterdir() if p.is_file()}
    pending = sorted(directory.glob('xtgv_*.exe'))
    if not pending:
        raise RuntimeError(f'No Windows executables to stage in {directory}')
    # The message hook is loaded dynamically, so it has no executable import.
    hook = directory / 'xtgv_key_hook.dll'
    if hook.is_file():
        pending.append(hook)
    selected = {}
    log.write('\n*** RUNTIME DLLS ***\n'); log.flush()
    while pending:
        binary = pending.pop()
        try:
            output = subprocess.check_output(
                [str(objdump), '-p', native_path(binary)], env=environment,
                stderr=subprocess.STDOUT, text=True, errors='replace')
        except subprocess.CalledProcessError as error:
            log.write(error.output); log.flush()
            raise
        for name in re.findall(r'^\s*DLL Name:\s*(\S+)\s*$', output, re.MULTILINE):
            key = name.casefold()
            if key in selected:
                continue
            if key in available:
                selected[key] = available[key]
                pending.append(available[key])
            elif key in system_names or key.startswith(('api-ms-win-', 'ext-ms-win-')):
                continue
            else:
                raise RuntimeError(f'Missing runtime DLL {name}, imported by {binary.name}; '
                                   f'not found in {runtime} or {system}')
    for source in sorted(selected.values()):
        shutil.copy2(source, directory / source.name)
        log.write(f'{source} -> {directory / source.name}\n')
    log.flush()
    return len(selected)


def build(args):
    library = host_path(args.gfx or os.environ.get('XTGV_GFX_SOURCE', GFX_ROOT))
    level = args.optimization if args.optimization != 'default' else 'O2'
    directory, commands = build_commands(args.target, library, level, args.jobs)
    if args.dry_run:
        print('Preview only; Windows execution is not validated by this output.')
        for command in commands:
            print(subprocess.list2cmdline(command))
        return
    require_msys()
    for program in ('cmake.exe', 'g++.exe', 'ninja.exe', 'objdump.exe'):
        if not (Path('/ucrt64/bin') / program).is_file():
            raise RuntimeError(f'Missing UCRT64 {program}; run tools-win/setup.cmd first')
    if not gfx_valid(library):
        raise RuntimeError('GFX 1.6.4 is missing; run setup.cmd or pass --gfx PATH')
    windows_root = next((v for k, v in os.environ.items() if k.casefold() == 'systemroot'), None)
    if not windows_root:
        raise RuntimeError('Windows SystemRoot is missing from the MSYS environment')
    system = host_path(windows_root) / 'System32'
    directory.mkdir(parents=True, exist_ok=True)
    log_path = directory / 'build.log'
    print(f'Desktop module: {TARGETS[args.target]["display_id"]} ({args.target})', flush=True)
    print(f'Optimization: -{level}', flush=True)
    print(f'Build details: {native_path(log_path)}', flush=True)
    # UCRT64 first also supplies compiler/SDL transitive DLL dependencies.
    environment = os.environ.copy()
    environment['PATH'] = '/ucrt64/bin:/usr/bin:' + environment.get('PATH', '')
    environment['MSYS2_ARG_CONV_EXCL'] = '*'
    try:
        with log_path.open('w', encoding='utf-8') as log:
            for section, command in zip(('CONFIGURE', 'BUILD'), commands):
                print('Configuring desktop ...' if section == 'CONFIGURE' else 'Building desktop ...', flush=True)
                log.write(f'\n*** {section} ***\n{command!r}\n'); log.flush()
                subprocess.run(command, env=environment, stdout=log,
                               stderr=subprocess.STDOUT, check=True)
            print("Staging runtime DLLs ...", flush=True)
            count = stage_runtime(directory, Path('/ucrt64/bin'),
                                  system,
                                  Path('/ucrt64/bin/objdump.exe'), environment, log)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f'Windows build failed; see {log_path}') from error
    finally:
        if getattr(args, 'log', None):
            adjacent = host_path(args.log)
            if adjacent.resolve() != log_path.resolve():
                append_log(adjacent, '\n*** BUILD DETAILS ***\n' + log_path.read_text(encoding='utf-8', errors='replace'))
    print('Building desktop: OK', flush=True)
    print(f'Built: {native_path(directory / "xtgv_desktop.exe")}')
    print(f'Copied {count} runtime DLLs beside the executables.')
    print('Ready for desktop-local.cmd or desktop-card.cmd; launch is separate.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    actions.add_parser('setup-gfx')
    builder = actions.add_parser('build')
    builder.add_argument('--target', required=True, type=target_name)
    builder.add_argument('--log', default=os.environ.get('XTGV_SHORTCUT_LOG'))
    builder.add_argument('--gfx')
    builder.add_argument('--optimization', '-O', type=optimization_level, default='O2')
    builder.add_argument('--jobs', type=int, default=2)
    builder.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    try:
        if args.action == 'setup-gfx':
            require_msys(); install_gfx()
        else:
            if args.jobs < 1:
                raise RuntimeError('--jobs must be positive')
            with shortcut_log(host_path(args.log) if args.log else None):
                if args.log: print(f'Log: {native_path(host_path(args.log))}', flush=True)
                build(args)
    except (OSError, RuntimeError, ValueError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
