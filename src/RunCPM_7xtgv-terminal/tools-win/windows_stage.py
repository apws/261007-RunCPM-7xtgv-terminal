#!/usr/bin/env python3
"""Copy one existing Windows runtime and a complete card to a fresh local folder."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from windows_build import REPO, SKETCH, OUTPUT, TARGETS, host_path, native_path, require_msys, target_name
from desktop_card_lock import LOCK_NAME, card_lock


# Runtime import closure, including the mirror's optional transfer panel.
# Preserve the small repository-shaped layout so existing path discovery works.
RUNTIME_FILES = (
    'tools-win/windows.cmd', 'tools-win/windows_run.py', 'tools-win/key_bridge.py',
    'tools-win/windows_build.py', 'tools-win/mintty_profile.py', 'tools-win/mintty_keys.py',
    'tools-win/shortcut_log.py',
    'desktop/desktop.py', 'tools/target_mirror.py', 'tools/target_config.py',
    'tools/terminal.py', 'tools/terminal.conf', 'tools/card_paths.py', 'tools/desktop_connection.py',
    'tools/desktop_card_lock.py', 'tools/key_event_ids.py', 'tools/host_key_config.py',
    'tools/cpm-transfer/transfer.py', 'tests/hardware_smoke.py',
    'src/terminal/key_chords.h',
)


def copy_file(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def write_cmd(path, text):
    path.write_bytes(text.replace('\n', '\r\n').encode('ascii'))


def verify_imports(sketch):
    # Isolate from the source tree and PYTHONPATH: a missing packaged helper
    # must not accidentally be satisfied by the live development checkout.
    subprocess.run([sys.executable, '-I', '-B', '-c',
                    'import sys; from pathlib import Path; '
                    'sys.path.insert(0, sys.argv[1]); '
                    'import windows_run, terminal, mintty_profile; '
                    'terminal.load_console_transfer(); '
                    'assert mintty_profile.bindings(); '
                    'assert len(windows_run.TARGETS) == 1; '
                    'assert windows_run.REPO == Path(sys.argv[2])',
                    str(sketch / 'tools-win'), str(sketch.parents[1])], check=True,
                   cwd=sketch.parents[1])


def stage(destination, target, source_repo=REPO, source_build=None):
    source_repo = Path(source_repo).resolve()
    source_sketch = source_repo / 'src' / SKETCH.name
    build = Path(source_build) if source_build else OUTPUT / 'msys2-ucrt64' / target
    card = source_repo / 'cpmcard'
    destination = Path(destination).resolve()
    if not (card / 'A/0').is_dir():
        raise RuntimeError(f'Source card needs A/0: {card}')
    # Never recurse into the source card when copying it.
    if destination == card or card in destination.parents:
        raise RuntimeError('Destination must be outside the source card.')
    runtime = [build / 'xtgv_desktop.exe', build / 'xtgv_key_bridge.exe'] + sorted(
        p for p in build.glob('*') if p.is_file() and p.suffix.lower() == '.dll')
    if not (build / 'xtgv_key_hook.dll').is_file():
        raise RuntimeError(f'Key helper DLL missing; rebuild first: {build}')
    if not any(p.name.lower() == 'sdl2.dll' for p in runtime):
        raise RuntimeError(f'SDL2.dll missing beside the executable; rebuild first: {build}')
    helpers = [source_sketch / p for p in RUNTIME_FILES]
    helpers.append(source_sketch / f'tools/targets/{target}.json')
    label = f'{TARGETS[target]["display_id"]} -- {target}'
    launchers = [source_sketch / 'tools-win/targets' / label / f'desktop-{mode}.cmd'
                 for mode in ('local', 'card')]
    for p in runtime + helpers + launchers:
        if not p.is_file():
            raise RuntimeError(f'Required runtime file missing: {p}')

    # Refuse an active source-card writer; never stop a session or copy its lock.
    with card_lock(card):
        destination.mkdir(parents=True, exist_ok=True)
        root = Path(tempfile.mkdtemp(
            prefix=f'{TARGETS[target]["display_id"]}-{time.strftime("%y%m%d-%H%M")}-',
            dir=destination))
        print(f'Copying runtime and card to: {native_path(root)}', flush=True)
        sketch = root / 'src' / SKETCH.name
        for p in helpers + launchers:
            copy_file(p, sketch / p.relative_to(source_sketch))
        output = root / 'build' / SKETCH.name / 'desktop-win/msys2-ucrt64' / target
        for p in runtime:
            copy_file(p, output / p.name)
        shutil.copytree(card, root / 'cpmcard', ignore=lambda path, names:
                        [LOCK_NAME] if Path(path) == card else [])

    # This is a launch-only snapshot: do not expose rebuild/setup actions that
    # would require sources, caches or toolchains omitted from this copy.
    dispatch = sketch / 'tools-win/dispatch.sh'
    dispatch.write_text(
        '#!/usr/bin/env bash\nset -euo pipefail\n'
        'xtgv_tools=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)\n'
        'if [[ "${1:-}" != launch ]]; then\n'
        '    echo "Launch-only copy; rebuild and restage from the source project." >&2\n'
        '    exit 2\nfi\nshift\n'
        'exec /usr/bin/python3 "$xtgv_tools/windows_run.py" launch "$@"\n')
    for name in ('LICENSE', 'LICENSING.adoc', 'THIRD_PARTY_NOTICES.md'):
        copy_file(source_repo / name, root / name)
    shutil.copytree(source_repo / 'LICENSES', root / 'LICENSES')
    verify_imports(sketch)

    for mode in ('local', 'card'):
        write_cmd(root / f'desktop-{mode}.cmd',
                  '@echo off\nsetlocal\n'
                  + 'set "XTGV_SHORTCUT_LOG=%~dpn0.log"\n'
                  + f'call "%~dp0src\\{SKETCH.name}\\tools-win\\windows.cmd" '
                  f'launch --target {target} --card-mode {mode} %*\n'
                  'exit /b %ERRORLEVEL%\n')
    (root / 'README.txt').write_text(
        f'7xtgv-terminal {TARGETS[target]["display_id"]}: local runtime snapshot\n\n'
        'Run desktop-local.cmd to use this folder\'s cpmcard (changes persist here).\n'
        'Run desktop-card.cmd --card-root "DRIVE:\\path\\to\\card" for another card.\n'
        'Existing MSYS2 is still required; it is discovered by windows.cmd.\n'
        'No rebuild needed. No runtime access to the original project is required.\n'
        f'Logs: build/{SKETCH.name}/desktop-win/msys2-ucrt64/{target}/sessions/\n'
        'Logs and card changes stay local; nothing is synchronized back to W:.\n'
        'Rebuild in the source project, then restage to obtain a fresh snapshot.\n'
        'This is a personal test copy, not a standalone release package.\n')
    files = {}
    for p in sorted(root.rglob('*')):
        if p.is_file():
            files[p.relative_to(root).as_posix()] = dict(
                bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
    (root / 'stage-manifest.json').write_text(json.dumps(dict(
        target=target, source_repo=str(source_repo), source_build=str(build),
        created=time.strftime('%Y-%m-%d %H:%M:%S'), files=files), indent=2) + '\n')
    size = sum(info['bytes'] for info in files.values())
    print(f'Ready: {native_path(root / "desktop-local.cmd")}\n'
          f'{len(files)} files, {size / (1024 * 1024):.1f} MiB. '
          'Original card preserved; no session started.', flush=True)
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', default='ES3_5')
    parser.add_argument('--destination', default=r'D:\~tmp\260906-7xtgv',
                        help='Parent for a fresh dated target directory')
    args = parser.parse_args()
    try:
        require_msys()
        stage(host_path(args.destination), target_name(args.target))
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'Windows staging: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
