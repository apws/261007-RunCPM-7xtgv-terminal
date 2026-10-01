"""No real ports/uploads: exercise the guard with PTYs and mocked Arduino CLI."""
import contextlib
import io
import json
import os
from pathlib import Path
import pty
import subprocess
import sys
import tempfile
import threading
import tty
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import firmware_identity as identity
import reflash


class IdentityTests(unittest.TestCase):
    def test_transfer_mirror_ignores_identity(self):
        import hardware_smoke
        screen = hardware_smoke.Mirror()
        data = b'\x1bP7xtgv-id;1;jc8048w550;260924-1259\x1b\\\x1b[2J\x1b[HOK'
        for byte in data:
            screen.feed(bytes([byte]))
        self.assertEqual(screen.text().strip(), 'OK')

    def test_fresh_fragmented_reply_and_stale_input(self):
        master, slave = pty.openpty()
        tty.setraw(slave)
        os.write(master, b'\x1bP7xtgv-id;1;jc8048w535;260924-0000\x1b\\')
        def device():
            query = b''
            while len(query) < len(identity.QUERY):
                query += os.read(master, 100)
            self.assertEqual(query, identity.QUERY)
            for byte in b'ANSI noise\x1bP7xtgv-id;1;jc8048w550;260924-1259\x1b\\':
                os.write(master, bytes([byte]))
        worker = threading.Thread(target=device, daemon=True)
        worker.start()
        try:
            self.assertEqual(identity.query_fd(slave, .5), ('jc8048w550', '260924-1259'))
            worker.join(.5)
            self.assertFalse(worker.is_alive())
        finally:
            os.close(slave)
            os.close(master)

    def test_no_identity_reply(self):
        master, slave = pty.openpty()
        tty.setraw(slave)
        try:
            with self.assertRaises(identity.IdentityUnavailable):
                identity.query_fd(slave, .02)
        finally:
            os.close(slave)
            os.close(master)

    def test_target_check_and_explicit_recovery(self):
        with patch.object(identity, 'query_firmware', return_value=('jc8048w550', 'unstamped')):
            identity.require_target('fake', 'jc8048w550')
            for manual in (None, 'jc8048w535'):
                with self.assertRaises(identity.TargetMismatch):
                    identity.require_target('fake', 'jc8048w535', manual)
        with patch.object(identity, 'query_firmware', side_effect=identity.IdentityUnavailable('old')):
            for manual in (None, 'wrong'):
                with self.assertRaises(identity.IdentityUnavailable):
                    identity.require_target('fake', 'jc8048w550', manual)
            identity.require_target('fake', 'jc8048w550', 'jc8048w550')
        with patch.object(identity, 'query_firmware', side_effect=PermissionError('busy')):
            with self.assertRaises(PermissionError):
                identity.require_target('fake', 'jc8048w550', 'jc8048w550')


class DispatcherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        cli = patch.object(reflash, 'arduino_cli', return_value='arduino-cli')
        cli.start()
        self.addCleanup(cli.stop)
        self.root = Path(self.temp.name) / 'src' / 'project'
        self.root.mkdir(parents=True)
        self.targets = self.root / 'targets'
        self.targets.mkdir()
        (self.targets / 'jc8048w550.json').write_text(json.dumps({
            'method': 'arduino-cli', 'profile': 'jc8048w550', 'port': 'fake'}))
        self.images = Path(self.temp.name) / 'build' / 'project' / 'jc8048w550'
        for name in ('new', 'current', 'archive'):
            (self.images / name).mkdir(parents=True)
        for attr, value in (('ROOT', self.root), ('TARGETS', self.targets)):
            mock = patch.object(reflash, attr, value)
            mock.start()
            self.addCleanup(mock.stop)

    def invoke(self, *args):
        with patch.object(sys, 'argv', ['reflash.py', '--target', 'jc8048w550', *args]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return reflash.main()

    def test_mismatch_blocks_all_existing_image_paths_before_mutation(self):
        for args in ((), ('--current',), ('--promote-new',), ('--restore', 'archive')):
            with patch.object(reflash, 'require_target', side_effect=identity.TargetMismatch('wrong')) as guard, \
                    patch.object(reflash.subprocess, 'run') as upload:
                self.assertEqual(self.invoke(*args), 1)
                guard.assert_called_once_with('fake', 'jc8048w550', None)
                upload.assert_not_called()
                self.assertEqual(sorted(p.name for p in self.images.iterdir()), ['archive', 'current', 'new'])

    def test_normal_and_manual_uploads_use_guard(self):
        for args, manual in (((), None), (('--allow-unidentified-target', 'jc8048w550'), 'jc8048w550')):
            with patch.object(reflash, 'require_target') as guard, \
                    patch.object(reflash.subprocess, 'run') as upload:
                upload.return_value.returncode = 0
                self.assertEqual(self.invoke(*args), 0)
                guard.assert_called_once_with('fake', 'jc8048w550', manual)
                self.assertEqual(upload.call_args.args[0][1], 'upload')

    def test_build_only_offline_and_build_upload_checks_after_compile(self):
        for build_only in (True, False):
            calls = []
            def cli(command, **kwargs):
                if '--show-properties=expanded' in command:
                    calls.append('properties')
                    return type('Result', (), {'returncode': 0,
                        'stdout': 'build.extra_flags=-DARDUINO_USB_CDC_ON_BOOT=1\n'})()
                calls.append(command[1])
                if command[1] == 'compile':
                    Path(command[command.index('--output-dir') + 1]).mkdir(parents=True)
                return type('Result', (), {'returncode': 0})()
            def guard(*args):
                calls.append('guard')
                raise identity.TargetMismatch('changed board')
            with patch.object(reflash.subprocess, 'run', side_effect=cli), \
                    patch.object(reflash, 'require_target', side_effect=guard):
                self.assertEqual(self.invoke('--build-only' if build_only else '--build'), 0 if build_only else 1)
                self.assertEqual(calls, ['properties', 'compile'] if build_only else
                                 ['properties', 'compile', 'guard'])
            # Avoid archive timestamp collision between these simulated builds.
            for path in self.images.iterdir():
                if path.name.endswith(' unverified'):
                    path.rmdir()

    def test_dry_run_never_opens_port(self):
        with patch.object(reflash, 'require_target') as guard, patch.object(reflash.subprocess, 'run') as cli:
            self.assertEqual(self.invoke('--dry-run'), 0)
            guard.assert_not_called()
            cli.assert_not_called()

    def test_bootsel_build_only_is_offline_but_upload_paths_remain_blocked(self):
        target = 'rp2350-touch-lcd-2'
        (self.targets / (target + '.json')).write_text(json.dumps({
            'method': 'bootsel-uf2', 'profile': target}))
        calls = []
        def cli(command, **kwargs):
            if '--show-properties=expanded' in command:
                return subprocess.CompletedProcess(command, 0, 'build.extra_flags=\n', '')
            calls.append(command[1])
            output = Path(command[command.index('--output-dir') + 1])
            output.mkdir(parents=True)
            (output / 'test.uf2').write_bytes(b'fixture')
            return subprocess.CompletedProcess(command, 0)
        with patch.object(reflash, 'resolve_port') as port, \
                patch.object(reflash, 'require_target') as guard, \
                patch.object(reflash.subprocess, 'run', side_effect=cli):
            self.assertEqual(self.invoke('--target', target, '--build-only'), 0)
            port.assert_not_called()
            guard.assert_not_called()
        self.assertEqual(calls, ['compile'])
        self.assertTrue((self.images.parent / target / 'new/test.uf2').is_file())
        with patch.object(reflash.subprocess, 'run') as runner:
            for args in ((), ('--build',), ('--current',), ('--promote-new',),
                         ('--restore', 'archive')):
                with self.subTest(args=args), self.assertRaises(SystemExit):
                    self.invoke('--target', target, *args)
            runner.assert_not_called()

    def test_manual_target_must_match_selected_target(self):
        with self.assertRaises(SystemExit):
            self.invoke('--allow-unidentified-target', 'jc8048w535')

    def test_w535_sdmmc_fallback_flag_is_build_only_and_explicit(self):
        (self.targets / 'jc8048w535.json').write_text(json.dumps({
            'method': 'arduino-cli', 'profile': 'jc8048w535', 'port': 'fake'}))
        for args in (('--w535-sd-mmc',),
                     ('--target', 'jc8048w535', '--w535-sd-mmc'),
                     ('--build-only', '--w535-sd-mmc')):
            with self.subTest(args=args), self.assertRaises(SystemExit):
                self.invoke(*args)
        with patch.object(reflash, 'resolved_build_properties',
                          return_value={'build.extra_flags': '-DPLATFORM=1 -DXTGV_BUILD_STAMP=x'}):
            output = io.StringIO()
            with patch.object(sys, 'argv', ['reflash.py', '--target', 'jc8048w535',
                                           '--build-only', '--w535-sd-mmc', '--dry-run']), \
                    contextlib.redirect_stdout(output):
                self.assertEqual(reflash.main(), 0)
        self.assertIn('-DPLATFORM=1 -DXTGV_BUILD_STAMP=x -DXTGV_W535_SD_MMC=1',
                      output.getvalue())

    def test_legacy_wrapper_routes_through_guarded_dispatcher(self):
        wrapper = (TOOLS / 'reflash-jc8048w535.sh').read_text()
        self.assertIn('reflash.py', wrapper)
        self.assertNotIn('"$arduino_cli" upload', wrapper)


if __name__ == '__main__':
    unittest.main()
