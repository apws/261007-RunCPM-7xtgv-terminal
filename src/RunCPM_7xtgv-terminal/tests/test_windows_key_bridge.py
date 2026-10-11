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


if __name__ == '__main__':
    unittest.main()
