"""Build/run the native host storage probe without rebuilding the emulator."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from windows_build import OUTPUT, SKETCH, host_path, native_path, require_msys, stage_runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--local-root', default=r'D:\~tmp\260906-7xtgv')
    args = parser.parse_args()
    require_msys()
    compiler = Path('/ucrt64/bin/g++.exe')
    if not compiler.is_file():
        raise RuntimeError('UCRT64 compiler missing; run setup.cmd first.')
    parent = OUTPUT / 'storage-operation-proofs'
    parent.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix='profile-', dir=parent))
    local_parent = host_path(args.local_root)
    local_parent.mkdir(parents=True, exist_ok=True)
    local = Path(tempfile.mkdtemp(prefix='storage-profile-', dir=local_parent))
    environment = os.environ.copy()
    environment['PATH'] = '/ucrt64/bin:/usr/bin:' + environment.get('PATH', '')
    environment['MSYS2_ARG_CONV_EXCL'] = '*'
    binary = evidence / 'xtgv_storage_profile.exe'
    contract = evidence / 'xtgv_storage_reuse_test.exe'
    command = [str(compiler), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-municode',
               native_path(SKETCH / 'desktop/tests/storage_profile.cpp'), '-o', native_path(binary)]
    contract_command = [str(compiler), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + native_path(SKETCH / 'desktop'),
                        native_path(SKETCH / 'desktop/tests/storage_reuse_test.cpp'),
                        native_path(SKETCH / 'desktop/storage.cpp'), '-o', native_path(contract)]
    print(f'Building storage probe and correctness test. Evidence: {native_path(evidence)}', flush=True)
    with (evidence / 'build.log').open('w') as log:
        log.write(repr(command) + '\n'); log.flush()
        subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
        log.write(repr(contract_command) + '\n'); log.flush()
        subprocess.run(contract_command, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
        system_root = next(v for k, v in environment.items() if k.casefold() == 'systemroot')
        stage_runtime(evidence, Path('/ucrt64/bin'), host_path(system_root) / 'System32',
                      Path('/ucrt64/bin/objdump.exe'), environment, log)
    for p in evidence.iterdir():
        if p.suffix.lower() in ('.dll', '.exe'):
            shutil.copy2(p, local / p.name)
    print(f'Running on local storage: {native_path(local)}', flush=True)
    for variable in ('TMPDIR', 'TEMP', 'TMP'):
        environment[variable] = native_path(local)
    with (evidence / 'correctness.log').open('wb') as log:
        subprocess.run([str(local / contract.name)], env=environment, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    print((evidence / 'correctness.log').read_text(encoding='utf-8'), end='')
    with (evidence / 'profile.csv').open('wb') as log:
        subprocess.run([str(local / binary.name), native_path(local)], env=environment,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    print((evidence / 'profile.csv').read_text(encoding='utf-8'), end='')
    print(f'Complete. Shared results: {native_path(evidence / "profile.csv")}', flush=True)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'Storage profile failed: {error}', file=sys.stderr)
        raise SystemExit(1)
