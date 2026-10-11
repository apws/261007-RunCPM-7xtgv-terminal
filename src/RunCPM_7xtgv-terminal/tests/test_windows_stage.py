"""Relocatable runtime closure and source/destination card preservation."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

SKETCH = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SKETCH / 'tools-win'))
import windows_stage as stage


class WindowsStageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.destination = self.root / 'local folder with spaces'
        self.target = 'jc8048w550'
        self.label = 'ES3_5 -- jc8048w550'
        for name in (*stage.RUNTIME_FILES, f'tools/targets/{self.target}.json',
                     *(f'tools-win/targets/{self.label}/desktop-{mode}.cmd'
                       for mode in ('local', 'card'))):
            stage.copy_file(SKETCH / name, self.source / 'src' / SKETCH.name / name)
        for name in ('LICENSE', 'LICENSING.adoc', 'THIRD_PARTY_NOTICES.md'):
            stage.copy_file(stage.REPO / name, self.source / name)
        shutil.copytree(stage.REPO / 'LICENSES', self.source / 'LICENSES')
        self.card = self.source / 'cpmcard'
        (self.card / 'A/0').mkdir(parents=True)
        (self.card / 'A/0/7XTGVTRM.INI').write_bytes(b'INI\r\n\x1a')
        (self.card / 'A/0/7XTGVTRM.BUF').write_bytes(b'\x00\xffhistory')
        (self.card / 'private').mkdir()
        (self.card / 'private/retained.bin').write_bytes(b'complete card')
        (self.source / 'private').mkdir()
        (self.source / 'private/excluded.txt').write_text('repository private')
        self.build = self.root / 'build'
        self.build.mkdir()
        for name in ('xtgv_desktop.exe', 'xtgv_key_bridge.exe', 'xtgv_key_hook.dll',
                     'SDL2.dll', 'libwinpthread-1.dll'):
            (self.build / name).write_bytes(b'fixture ' + name.encode())
        (self.build / 'build.ninja').write_text('must not be copied')

    def run_stage(self):
        return stage.stage(self.destination, self.target, self.source, self.build)

    def test_runtime_relocation_and_repeat_preserves_previous_card(self):
        first = self.run_stage()  # Also tests isolated imports + transfer/key helpers.
        for p in self.card.rglob('*'):
            if p.is_file():
                self.assertEqual(p.read_bytes(), (first / 'cpmcard' / p.relative_to(self.card)).read_bytes())
        self.assertFalse((first / 'private').exists())
        self.assertFalse(list(first.rglob('build.ninja')))
        self.assertFalse(list(first.rglob('__pycache__')))
        self.assertFalse((self.card / stage.LOCK_NAME).exists())
        self.assertFalse((first / 'cpmcard' / stage.LOCK_NAME).exists())
        (first / 'cpmcard/A/0/7XTGVTRM.BUF').write_bytes(b'local changes')
        second = self.run_stage()
        self.assertNotEqual(first, second)
        self.assertEqual((first / 'cpmcard/A/0/7XTGVTRM.BUF').read_bytes(), b'local changes')
        self.assertEqual((second / 'cpmcard/A/0/7XTGVTRM.BUF').read_bytes(), b'\x00\xffhistory')
        manifest = json.loads((first / 'stage-manifest.json').read_text())
        self.assertEqual(manifest['target'], self.target)
        self.assertIn('cpmcard/private/retained.bin', manifest['files'])
        command = (first / 'desktop-local.cmd').read_bytes()
        self.assertIn(b'--card-mode local', command)
        self.assertNotIn(b'\n', command.replace(b'\r\n', b''))

    def test_active_source_card_is_preserved(self):
        with stage.card_lock(self.card):
            owner = (self.card / stage.LOCK_NAME / 'owner.json').read_bytes()
            with self.assertRaisesRegex(RuntimeError, 'already locked'):
                self.run_stage()
            self.assertEqual((self.card / stage.LOCK_NAME / 'owner.json').read_bytes(), owner)
            self.assertFalse(self.destination.exists())

    def test_missing_runtime_and_recursive_destination_are_rejected(self):
        self.destination = self.card / 'nested'
        with self.assertRaisesRegex(RuntimeError, 'outside the source card'):
            self.run_stage()
        self.destination = self.root / 'elsewhere'
        (self.build / 'SDL2.dll').unlink()
        with self.assertRaisesRegex(RuntimeError, 'SDL2.dll missing'):
            self.run_stage()
        self.assertFalse(self.destination.exists())


if __name__ == '__main__':
    unittest.main()
