"""Private helper lifecycle, session selection and failure cleanup; no native GUI."""
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools-win'))
import key_bridge
from mintty_profile import window_class, write_profile


class KeyBridgeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = self.root / 'mintty.conf'
        write_profile(self.profile)
        for name in ('xtgv_key_bridge.exe', 'xtgv_key_hook.dll'):
            (self.root / name).touch()

    def test_toprow_literal_setting_environment_override_and_disable(self):
        config = self.root / 'terminal.conf'
        config.write_text("XTGV_TARGET=unrelated\nXTGV_HACK_TOPROW='+ěščřžýáíé' # Czech\nexport XTGV_HACK_TOPROW\n")
        self.assertEqual(key_bridge.mintty_environment(config, {})['XTGV_HACK_TOPROW'], '+ěščřžýáíé')
        for value in ('', '+ľščťžýáíé'):
            env = {'XTGV_HACK_TOPROW': value, 'PRESERVE': 'yes'}
            self.assertEqual(key_bridge.mintty_environment(config, env), env)
        config.write_text("XTGV_HACK_TOPROW=''\n")
        self.assertEqual(key_bridge.mintty_environment(config, {})['XTGV_HACK_TOPROW'], '')
        # Missing option disables plain row conversion, without inventing a layout.
        config.write_text('XTGV_BANK_KEYS=unchanged\n')
        self.assertEqual(key_bridge.mintty_environment(config, {})['XTGV_HACK_TOPROW'], '')

    def test_toprow_config_is_data_not_shell_code(self):
        config = self.root / 'terminal.conf'
        config.write_text("UNRELATED=$(not_a_command)\nexport XTGV_HACK_TOPROW='$(touch x)'\n")
        self.assertEqual(key_bridge.mintty_environment(config, {})['XTGV_HACK_TOPROW'], '$(touch x)')
        for literal in ("'short'", "'unterminated", "'1234567890' extra", "'123456789😀'"):
            config.write_text('XTGV_HACK_TOPROW=' + literal + '\n')
            with self.assertRaisesRegex(RuntimeError, 'XTGV_HACK_TOPROW'):
                key_bridge.mintty_environment(config, {})

    def test_unique_class_is_same_for_owner_and_profile(self):
        name = window_class(self.profile)
        self.assertIn('Class=' + name + '\n', self.profile.read_text())
        self.assertNotEqual(name, window_class(self.root / 'other' / 'mintty.conf'))

    def test_linux_never_starts_native_hook(self):
        with patch.object(key_bridge.sys, 'platform', 'linux'), patch.object(key_bridge.subprocess, 'Popen') as popen:
            with key_bridge.KeyBridge(self.root, self.profile):
                pass
            popen.assert_not_called()

    def test_missing_artifact_requests_rebuild_before_start(self):
        (self.root / 'xtgv_key_hook.dll').unlink()
        with patch.object(key_bridge.sys, 'platform', 'cygwin'), patch.object(key_bridge.subprocess, 'Popen') as popen:
            with self.assertRaisesRegex(RuntimeError, 'desktop-rebuild.cmd'):
                with key_bridge.KeyBridge(self.root, self.profile):
                    pass
            popen.assert_not_called()

    def test_success_and_failure_release_pipe_and_wait(self):
        for ready in (b'READY\r\n', b''):
            process = Mock(stdin=io.BytesIO(), stdout=io.BytesIO(ready))
            with patch.object(key_bridge.sys, 'platform', 'cygwin'), \
                    patch.object(key_bridge, 'native_path', side_effect=str), \
                    patch.object(key_bridge.subprocess, 'Popen', return_value=process) as popen:
                bridge = key_bridge.KeyBridge(self.root, self.profile)
                with self.assertRaisesRegex(RuntimeError, 'fixture|helper failed'):
                    with bridge:
                        raise RuntimeError('fixture')
                command = popen.call_args.args[0]
                self.assertEqual(command[1], window_class(self.profile))
                self.assertTrue(process.stdin.closed)
                self.assertTrue(process.stdout.closed)
                process.wait.assert_called_once_with(timeout=5)
                process.terminate.assert_not_called()
                self.assertIn('Stopped', (self.root / 'key-bridge.log').read_text())

    def test_native_configuration_is_explicit_ascii_argument(self):
        for row, encoded in (('+ěščřžýáíé', '002b011b0161010d0159017e00fd00e100ed00e9'), ('', '-')):
            process = Mock(stdin=io.BytesIO(), stdout=io.BytesIO(b'READY\n'))
            with patch.object(key_bridge.sys, 'platform', 'cygwin'), \
                    patch.dict(key_bridge.os.environ, {'XTGV_HACK_TOPROW': row}), \
                    patch.object(key_bridge, 'native_path', side_effect=str), \
                    patch.object(key_bridge.subprocess, 'Popen', return_value=process) as popen:
                with key_bridge.KeyBridge(self.root, self.profile):
                    self.assertEqual(popen.call_args.args[0][3], encoded)


if __name__ == '__main__':
    unittest.main()
