"""Offline Windows tooling contracts; does not claim native CMD/MSYS execution."""
import argparse
import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile

SKETCH = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('windows_build', SKETCH / 'tools-win/windows_build.py')
win = importlib.util.module_from_spec(spec)
spec.loader.exec_module(win)


def archive_bytes():
    data = io.BytesIO()
    with zipfile.ZipFile(data, 'w') as package:
        package.writestr('GFX_Library_for_Arduino-1.6.4/library.properties', 'version=1.6.4\n')
        package.writestr('GFX_Library_for_Arduino-1.6.4/src/Arduino_GFX.cpp', '// fixture\n')
    return data.getvalue()


class WindowsBuildTools(unittest.TestCase):
    def test_build_progress_hides_paths_but_keeps_full_adjacent_log(self):
        with tempfile.TemporaryDirectory(prefix='build with spaces ') as temp:
            root = Path(temp)
            args = argparse.Namespace(target='jc8048w550', gfx=temp, optimization='O2',
                                      jobs=2, dry_run=False, log=str(root/'desktop-rebuild.log'))
            commands = [[win.sys.executable, '-c', 'print("configured")'],
                        [win.sys.executable, '-c', 'print("[1/2] compiler detail\\n[2/2] linked")']]
            screen = io.StringIO()
            with mock.patch.object(win, 'build_commands', return_value=(root/'build', commands)), \
                    mock.patch.object(win, 'require_msys'), mock.patch.object(win, 'gfx_valid', return_value=True), \
                    mock.patch.object(Path, 'is_file', return_value=True), \
                    mock.patch.dict(win.os.environ, {'SystemRoot': str(root/'Windows')}), \
                    mock.patch.object(win, 'stage_runtime', return_value=3), \
                    contextlib.redirect_stdout(screen):
                win.build(args)
            self.assertNotIn(temp, screen.getvalue())
            self.assertNotIn('compiler detail', screen.getvalue())
            self.assertIn('100%', screen.getvalue())
            self.assertIn('Building desktop: OK', screen.getvalue())
            log = Path(args.log).read_text()
            self.assertIn(str(root/'build'), log)
            self.assertIn('compiler detail', log)

    def test_runtime_closure_refreshes_dlls_and_excludes_windows_libraries(self):
        with tempfile.TemporaryDirectory(prefix='runtime with spaces ') as temp:
            output, runtime, system = [Path(temp)/n for n in ('output', 'ucrt64', 'System32')]
            for path in (output, runtime, system):
                path.mkdir()
            (output/'xtgv_desktop.exe').touch()
            (output/'xtgv_headless.exe').touch()
            (output/'xtgv_key_hook.dll').write_bytes(b'project hook')
            (output/'libwinpthread-1.dll').write_bytes(b'stale')
            (output/'unrelated.txt').write_bytes(b'preserve')
            for name in ('SDL2.dll', 'libwinpthread-1.dll', 'libstdc++-6.dll', 'unused.dll'):
                (runtime/name).write_bytes(name.encode())
            (system/'KERNEL32.dll').touch()
            imports = {
                'xtgv_desktop.exe': ['sdl2.DLL', 'libstdc++-6.dll'],
                'xtgv_headless.exe': ['libstdc++-6.dll'],
                'xtgv_key_hook.dll': ['KERNEL32.dll'],
                'SDL2.dll': ['KERNEL32.dll'],
                'libstdc++-6.dll': ['libwinpthread-1.dll'],
                'libwinpthread-1.dll': ['libstdc++-6.dll', 'api-ms-win-crt-runtime-l1-1-0.dll'],
            }
            def inspect(command, **kwargs):
                return '\n'.join('  DLL Name: ' + name for name in imports[Path(command[-1]).name])
            with mock.patch.object(win.subprocess, 'check_output', side_effect=inspect) as inspect_mock:
                count = win.stage_runtime(output, runtime, system, Path('objdump'), {}, io.StringIO())
            self.assertEqual(count, 3)
            self.assertEqual(inspect_mock.call_count, 6)
            self.assertEqual((output/'xtgv_key_hook.dll').read_bytes(), b'project hook')
            self.assertEqual((output/'libwinpthread-1.dll').read_bytes(), b'libwinpthread-1.dll')
            self.assertEqual((output/'unrelated.txt').read_bytes(), b'preserve')
            self.assertFalse((output/'KERNEL32.dll').exists())
            self.assertFalse((output/'unused.dll').exists())

    def test_missing_runtime_is_not_hidden_by_stale_output_copy(self):
        with tempfile.TemporaryDirectory() as temp:
            output, runtime, system = [Path(temp)/n for n in ('output', 'runtime', 'system')]
            for path in (output, runtime, system):
                path.mkdir()
            (output/'xtgv_desktop.exe').touch()
            (output/'missing.dll').write_bytes(b'stale')
            with mock.patch.object(win.subprocess, 'check_output', return_value='DLL Name: missing.dll\n'):
                with self.assertRaisesRegex(RuntimeError, 'Missing runtime DLL missing.dll'):
                    win.stage_runtime(output, runtime, system, Path('objdump'), {}, io.StringIO())

    def test_all_target_launchers_use_shared_metadata_and_bootstrap(self):
        folders = list((SKETCH / 'tools-win/targets').iterdir())
        self.assertEqual(len(folders), len(win.TARGETS))
        for key, data in win.TARGETS.items():
            folder = SKETCH / 'tools-win/targets' / f'{data["display_id"]} -- {key}'
            self.assertEqual(sorted(p.name for p in folder.glob('*.cmd')),
                             ['desktop-card.cmd', 'desktop-local.cmd', 'desktop-rebuild.cmd'])
            body = (folder / 'desktop-rebuild.cmd').read_bytes()
            self.assertIn(f'build --target {key} %*'.encode(), body)
            self.assertIn(b'"%~dp0..\\..\\windows.cmd"', body)
            self.assertNotIn(b'\n', body.replace(b'\r\n', b''))
            for mode in ('local', 'card'):
                body = (folder / f'desktop-{mode}.cmd').read_bytes()
                self.assertIn(f'launch --target {key} --card-mode {mode} '.encode(), body)
                launch = next(line for line in body.splitlines() if b' launch ' in line)
                self.assertTrue(launch.endswith(b'%*'), launch)
                self.assertIn(b'"%~dp0..\\..\\windows.cmd"', body)
                self.assertNotIn(b'\n', body.replace(b'\r\n', b''))

    def test_native_build_has_explicit_ucrt_tools_and_separate_cache(self):
        with mock.patch.object(win, 'native_path', side_effect=lambda p: 'C:/MSYS' + str(p)):
            directory, commands = win.build_commands('jc8012p4a1c', Path('/gfx with spaces'), 'Og', 4)
        self.assertEqual(directory.parent.name, 'msys2-ucrt64')
        self.assertIn('-DXTGV_GFX_SOURCE=C:/MSYS/gfx with spaces', commands[0])
        self.assertIn('-DCMAKE_CXX_COMPILER=C:/MSYS/ucrt64/bin/g++.exe', commands[0])
        self.assertIn('-DXTGV_OPTIMIZATION=Og', commands[0])
        self.assertEqual(commands[1][-2:], ['--parallel', '4'])
        self.assertFalse(any('TOOLCHAIN_FILE' in part for part in commands[0]))

    def test_dry_run_does_not_create_output_or_execute_build(self):
        with tempfile.TemporaryDirectory() as temp, mock.patch.object(win, 'OUTPUT', Path(temp)/'build'):
            args = argparse.Namespace(target='jc8012p4a1c', gfx=str(Path(temp)/'gfx'),
                                      optimization='O2', jobs=2, dry_run=True)
            with mock.patch.object(win.subprocess, 'run') as run, contextlib.redirect_stdout(io.StringIO()):
                win.build(args)
            run.assert_not_called()
            self.assertFalse(win.OUTPUT.exists())

    def test_wrong_platform_fails_before_build_or_output_creation(self):
        with tempfile.TemporaryDirectory() as temp, mock.patch.object(win, 'OUTPUT', Path(temp)/'build'):
            args = argparse.Namespace(target='jc8012p4a1c', gfx=str(Path(temp)/'gfx'),
                                      optimization='O2', jobs=2, dry_run=False)
            with mock.patch.object(win.sys, 'platform', 'linux'), mock.patch.object(win.subprocess, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'MSYS'):
                    win.build(args)
                run.assert_not_called()
            self.assertFalse(win.OUTPUT.exists())

    def test_gfx_installs_checked_archive_once(self):
        data = archive_bytes()
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            destination = Path(temp)/'gfx'
            with mock.patch.object(win, 'GFX_SHA256', hashlib.sha256(data).hexdigest()), \
                    mock.patch.object(win.urllib.request, 'urlopen', return_value=io.BytesIO(data)) as download:
                win.install_gfx(destination)
                win.install_gfx(destination)
                self.assertTrue(win.gfx_valid(destination))
                download.assert_called_once()

    def test_gfx_rejects_bad_hash_and_preserves_existing_directory(self):
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            destination = Path(temp)/'gfx'
            with mock.patch.object(win.urllib.request, 'urlopen', return_value=io.BytesIO(b'bad')):
                with self.assertRaisesRegex(RuntimeError, 'checksum'):
                    win.install_gfx(destination)
            self.assertFalse(destination.exists())
            destination.mkdir()
            marker = destination/'user-data'
            marker.write_bytes(b'preserve')
            with self.assertRaisesRegex(RuntimeError, 'preserved'):
                win.install_gfx(destination)
            self.assertEqual(marker.read_bytes(), b'preserve')


if __name__ == '__main__':
    unittest.main()
