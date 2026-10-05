"""Offline workflow checks: no downloads, serial access or hardware writes."""
import contextlib
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import bootsel_upload
import target_config
import target_reflash
import reflash


def block(target, family=0xe48bff59):
    payload = target.encode() + b'\0'
    header = struct.pack('<8I', 0x0a324655, 0x9e5d5157, 0x2000,
                         0x10000000, len(payload), 0, 1, family)
    return header + payload + bytes(476 - len(payload)) + struct.pack('<I', 0x0ab16f30)


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        project = Path(temp.name) / 'src' / 'project'
        project.mkdir(parents=True)
        mock = patch.object(target_reflash, 'PROJECT', project)
        mock.start()
        self.addCleanup(mock.stop)
        tools = project / 'tools'
        (tools / 'targets').mkdir(parents=True)
        mock = patch.object(target_reflash, 'TOOLS', tools)
        mock.start()
        self.addCleanup(mock.stop)

    def test_guard_failure_is_short_and_not_flash_success(self):
        report = target_reflash.failure_summary(
            'Sketch uses 450627 bytes\n' + '+ long uploader command\n' +
            '7xtgv reflash blocked: No firmware target reply\n' +
            'For intentional recovery repeat with --allow-unidentified-target example\n')
        self.assertEqual(report, 'Flash NOT performed: No firmware target reply')
        self.assertNotIn('Sketch', report)

    def test_build_status_is_short_and_log_sections_are_delimited(self):
        target = 'esp32c6-lcd-1-47'
        image_dir = target_reflash.PROJECT.parents[1] / 'build' / 'project' / target / 'new'
        image_dir.mkdir(parents=True)
        (image_dir / 'project.ino.bin').write_bytes(b'build 261004-1944\0')
        output = io.StringIO()
        with patch.object(sys, 'argv', ['workflow', '--target', target, '--build-only']), \
                patch.object(target_reflash, 'command'), contextlib.redirect_stdout(output):
            target_reflash.main()
        self.assertEqual(output.getvalue().splitlines(), [
            'Module: EC6_147 (esp32c6-lcd-1-47)', 'Building fresh firmware ...',
            'Building fresh firmware: OK', 'Embedded build: 261004-1944'])
        text = (target_reflash.TOOLS / 'targets' / ('EC6_147 -- ' + target) /
                'rebuild.log').read_text()
        self.assertIn('\n\n*** BUILD ***\n', text)
        self.assertIn('Image: ', text)

    def test_uf2_identity_family_and_length(self):
        with tempfile.TemporaryDirectory() as temp:
            image = Path(temp) / 'image.uf2'
            image.write_bytes(block('rp2350-touch-lcd-2'))
            bootsel_upload.validate_uf2(image, 'rp2350-touch-lcd-2')
            with self.assertRaises(RuntimeError):
                bootsel_upload.validate_uf2(image, 'rp2350-lcd-1-47-a')
            for data in (b'bad', block('rp2350-touch-lcd-2', 0xe48bff56),
                         bytes(512)):
                image.write_bytes(data)
                with self.assertRaises(RuntimeError):
                    bootsel_upload.validate_uf2(image, 'rp2350-touch-lcd-2')

    def test_bootsel_ambiguity_and_chip_selection(self):
        paths = ['/dev/disk/by-id/usb-RPI_RP2350_C885F192AD4984B5-0:0-part1',
                 '/dev/disk/by-id/usb-RPI_RP2350_D60D9318CF30A586-0:0-part1']
        with self.assertRaises(RuntimeError):
            bootsel_upload.choose_disk(paths=paths)
        self.assertEqual(bootsel_upload.choose_disk('c885f192ad4984b5', paths)[1],
                         'C885F192AD4984B5')
        with self.assertRaises(RuntimeError):
            bootsel_upload.choose_disk('0000000000000000', paths)

    def test_bootsel_confirmation_requires_only_enter(self):
        output = io.StringIO()
        with patch.object(bootsel_upload.sys.stdin, 'isatty', return_value=True), \
                patch('builtins.input', return_value='') as prompt, \
                contextlib.redirect_stdout(output):
            bootsel_upload.confirm_bootsel('RP2_147')
        self.assertIn('RP2_147', output.getvalue())
        self.assertIn('BOOTSEL', output.getvalue())
        self.assertIn('Press Enter', prompt.call_args.args[0])
        with patch.object(bootsel_upload.sys.stdin, 'isatty', return_value=False):
            with self.assertRaisesRegex(RuntimeError, 'interactive'):
                bootsel_upload.confirm_bootsel('RP2_147')

    def test_missing_bootsel_prompts_before_mounting(self):
        with patch.object(bootsel_upload, 'validate_uf2'), \
                patch.object(bootsel_upload, 'confirm_bootsel', side_effect=KeyboardInterrupt), \
                patch.object(bootsel_upload, 'choose_disk',
                             side_effect=bootsel_upload.NoBootselDisk), \
                patch.object(bootsel_upload, 'mounted_path') as mount:
            with self.assertRaises(KeyboardInterrupt):
                bootsel_upload.upload('rp2350-lcd-1-47-a', 'image.uf2')
            mount.assert_not_called()

    def test_present_bootsel_skips_prompt(self):
        disk = ('/dev/fake', 'D60D9318CF30A586')
        with patch.object(bootsel_upload, 'choose_disk', return_value=disk), \
                patch.object(bootsel_upload, 'confirm_bootsel') as prompt:
            self.assertEqual(bootsel_upload.ready_disk('RP2_147'), disk)
            prompt.assert_not_called()

    def test_ambiguous_bootsel_does_not_prompt_or_guess(self):
        with patch.object(bootsel_upload, 'choose_disk', side_effect=RuntimeError('ambiguous')), \
                patch.object(bootsel_upload, 'confirm_bootsel') as prompt:
            with self.assertRaisesRegex(RuntimeError, 'ambiguous'):
                bootsel_upload.ready_disk('RP2_147')
            prompt.assert_not_called()

    def test_missing_serial_prompts_then_rediscovers(self):
        with patch.object(target_reflash, 'resolve_port',
                          side_effect=[target_reflash.NoSerialPort('absent'), '/dev/good']), \
                patch.object(target_reflash.Path, 'exists', return_value=True), \
                patch.object(bootsel_upload, 'confirm_connection') as prompt:
            self.assertEqual(target_reflash.ready_serial(None, 'c6', {'display_id': 'EC6_147'}),
                             '/dev/good')
            prompt.assert_called_once_with('EC6_147')

    def test_isolated_config_reaches_property_resolution(self):
        result = subprocess.CompletedProcess([], 0, 'build.extra_flags=-DCORE\n', '')
        with patch.dict(os.environ, {'XTGV_ARDUINO_CONFIG': '/isolated/config.yaml'}), \
                patch.object(reflash.subprocess, 'run', return_value=result) as run:
            reflash.resolved_build_properties('cli', {'profile': 'p'}, 'stamp')
            args = run.call_args.args[0]
            self.assertEqual(args[args.index('--config-file') + 1], '/isolated/config.yaml')

    def test_serial_group_reentry_runs_uploader_not_terminal(self):
        with patch.object(target_config.os, 'access', return_value=False), \
                patch.object(target_config.os, 'stat') as stat, \
                patch.object(target_config.pwd, 'getpwuid') as account, \
                patch.object(target_config.os, 'getgrouplist', return_value=[984]), \
                patch.object(target_config.os, 'getgroups', return_value=[]), \
                patch.object(target_config.os, 'getgid', return_value=1000), \
                patch.object(target_config.os, 'getegid', return_value=1000), \
                patch.object(target_config.shutil, 'which', return_value='/usr/bin/newgrp'), \
                patch.object(target_config.grp, 'getgrgid') as group, \
                patch.object(sys, 'argv', [str(TOOLS / 'reflash.py'), '--target', 'example']), \
                patch.object(target_config.os, 'execv', side_effect=SystemExit) as execv:
            stat.return_value.st_gid = 984
            account.return_value.pw_name = 'test'
            account.return_value.pw_gid = 1000
            group.return_value.gr_name = 'uucp'
            with self.assertRaises(SystemExit):
                target_config.ensure_serial_access('/dev/fake')
            self.assertIn('reflash.py', execv.call_args.args[1][-1])
            self.assertNotIn('terminal.py', execv.call_args.args[1][-1])

    def test_missing_group_permission_reports_remedy(self):
        with patch.object(target_config.os, 'access', return_value=False), \
                patch.object(target_config.os, 'stat', side_effect=FileNotFoundError):
            with self.assertRaisesRegex(RuntimeError, 'serial group'):
                target_config.ensure_serial_access('/dev/fake')

    def test_build_only_never_stops_mirror(self):
        with patch.object(sys, 'argv', ['workflow', '--target', 'esp32c6-lcd-1-47',
                                       '--build-only']), \
                patch.object(target_reflash, 'command') as command, \
                patch.object(target_reflash.subprocess, 'run') as run:
            self.assertEqual(target_reflash.main(), 0)
            self.assertIn('--build-only', command.call_args.args[0])
            run.assert_not_called()

    def test_dry_run_never_builds_or_touches_device(self):
        with patch.object(sys, 'argv', ['workflow', '--target', 'rp2350-touch-lcd-2',
                                       '--dry-run']), \
                patch.object(target_reflash, 'command') as command, \
                patch.object(target_reflash.subprocess, 'run') as run, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(target_reflash.main(), 0)
            command.assert_not_called()
            run.assert_not_called()

    def test_failed_upload_restores_correct_mirror(self):
        with patch.object(sys, 'argv', ['workflow', '--target', 'esp32c6-lcd-1-47',
                                       '--upload-only', '--mirror', 'konsole']), \
                patch.dict(os.environ, {'XTGV_PORT': '/dev/stale'}), \
                patch.object(target_reflash, 'resolve_port', return_value='/dev/good'), \
                patch.object(target_reflash.shutil, 'which', return_value='/usr/bin/tmux'), \
                patch.object(target_reflash.subprocess, 'run'), \
                patch.object(target_reflash.Path, 'exists', return_value=True), \
                patch.object(target_reflash, 'command',
                             side_effect=[subprocess.CalledProcessError(1, 'upload'), None]) as cmd:
            with self.assertRaisesRegex(RuntimeError, 'Uploading firmware failed'):
                target_reflash.main()
            restored = list(map(str, cmd.call_args.args[0]))
            self.assertIn('kon.sh', restored[0])
            self.assertEqual(restored[-4:], ['--port', '/dev/good', '--target',
                                            'esp32c6-lcd-1-47'])
            self.assertNotIn('XTGV_PORT', os.environ)

    def test_split_wrappers_dry_run(self):
        for folder in (TOOLS / 'targets').glob('* -- *'):
            if not folder.is_dir():
                continue
            for action in ('rebuild', 'reflash'):
                with self.subTest(folder=folder.name, action=action):
                    result = subprocess.run(
                        [str(folder / (action + '.sh')), '--dry-run'],
                        text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(folder.name.split(' -- ')[1], result.stdout)
                    self.assertEqual('Build:' in result.stdout, action == 'rebuild')

    def test_missing_tools_notify_without_installing(self):
        with patch.object(target_reflash, 'arduino_cli', side_effect=RuntimeError('missing')), \
                patch.object(target_reflash.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'setup.sh'):
                target_reflash.check_prerequisites(False, {'method': 'arduino-cli'})

    def test_setup_with_fake_installers(self):
        # Isolated command fixture: sudo and CLI cannot install or download anything.
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            commands = base / 'commands'
            commands.mkdir()
            for name in ('sudo', 'arduino-cli'):
                executable = commands / name
                executable.write_text(
                    '#!/bin/bash\n'
                    'printf "%s\\n" "$*" >> "$XTGV_TEST_COMMANDS"\n')
                executable.chmod(0o755)
            env = dict(os.environ, PATH=str(commands) + ':' + os.environ['PATH'],
                       ARDUINO_CLI=str(commands / 'arduino-cli'),
                       XTGV_TEST_COMMANDS=str(base / 'calls'),
                       XTGV_ARDUINO_CONFIG=str(base / 'config.yaml'))
            result = subprocess.run(['bash', str(TOOLS / 'setup.sh'), '--yes'],
                                    env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            calls = (base / 'calls').read_text()
            self.assertIn('pacman -S --needed', calls)
            self.assertNotIn('-Syu', calls)
            self.assertIn('esp32:esp32@3.1.0', calls)
            self.assertIn('rp2040:rp2040@5.4.4', calls)
            self.assertIn('SdFat@2.3.0', calls)
            self.assertIn('--config-file ' + str(base / 'config.yaml'), calls)
            self.assertNotIn('compile', calls)
            self.assertNotIn('upload', calls)

    def test_wrappers_shell_syntax(self):
        paths = [TOOLS / 'fresh-build.sh', *sorted((TOOLS / 'targets').glob('* -- */*.sh'))]
        self.assertEqual(len(paths), 13)
        for path in paths:
            subprocess.run(['bash', '-n', str(path)], check=True)

    def test_fresh_script_pipeline_with_fake_cli_and_local_clone(self):
        # Exercise the shell orchestration without downloading or compiling.
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            source = base / 'source'
            tools = source / 'src/RunCPM_7xtgv-terminal/tools'
            tools.mkdir(parents=True)
            (tools / 'target_config.py').write_text('print("test target")\n')
            (tools / 'reflash.py').write_text(
                'import os, pathlib\n'
                'p = pathlib.Path(os.environ["XTGV_ARDUINO_CONFIG"])\n'
                'assert p.parent != pathlib.Path.home()\n'
                'assert os.environ["ARDUINO_CLI"].endswith("fake-cli")\n'
                'print("FAKE BUILD PASSED")\n')
            subprocess.run(['git', 'init', '-q', str(source)], check=True)
            subprocess.run(['git', '-C', str(source), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(source), '-c', 'user.name=Test',
                            '-c', 'user.email=test@example.invalid', 'commit', '-qm',
                            'fixture'], check=True)
            cli = base / 'fake-cli'
            cli.write_text(
                '#!/usr/bin/env python3\n'
                'import json, os, sys\n'
                'with open(os.environ["TEST_CLI_LOG"], "a") as f:\n'
                '    f.write(json.dumps(sys.argv[1:]) + "\\n")\n'
                'print("FAKE CLI; no downloads")\n')
            cli.chmod(0o755)
            env = dict(os.environ, TMPDIR=temp, TEST_CLI_LOG=str(base / 'cli.log'),
                       ARDUINO_DIRECTORIES_DATA='/must-not-use',
                       XTGV_PORT='/must-not-use')
            result = subprocess.run(
                ['bash', str(TOOLS / 'fresh-build.sh'), '--source', str(source),
                 '--use-cli', str(cli), '--target', 'esp32c6-lcd-1-47'],
                env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('FAKE BUILD PASSED', result.stdout)
            import json
            commands = [json.loads(line) for line in (base / 'cli.log').read_text().splitlines()]
            for args in commands[1:]:
                self.assertIn('--config-file', args)
                self.assertTrue(args[args.index('--config-file') + 1].startswith(temp))
            self.assertTrue(any('esp32:esp32@3.1.0' in args for args in commands))
            self.assertFalse(any('rp2040:rp2040@5.4.4' in args for args in commands))


if __name__ == '__main__':
    unittest.main()
