import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('names', Path(__file__).resolve().parents[1] / 'card_names.py')
names = importlib.util.module_from_spec(spec)
spec.loader.exec_module(names)

class Names(unittest.TestCase):
    def test_explicit_rename_preserves_bytes_and_unrelated_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'a/b').mkdir(parents=True)
            (root / 'a/b/Test.com').write_bytes(b'\0\x1a\xff')
            (root / 'notes.txt').write_text('keep')
            changes, errors = names.inventory(root)
            self.assertFalse(errors)
            self.assertTrue((root / 'a/b/Test.com').exists())
            names.apply(changes)
            self.assertEqual((root / 'A/B/TEST.COM').read_bytes(), b'\0\x1a\xff')
            self.assertTrue((root / 'notes.txt').exists())
    def test_collision_and_unsupported_names_block_apply(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'A/0').mkdir(parents=True)
            for name in ('TEST.COM', 'test.com', 'longfilename.com'):
                (root / 'A/0' / name).touch()
            _, errors = names.inventory(root)
            self.assertEqual(len(errors), 2)

if __name__ == '__main__': unittest.main()
