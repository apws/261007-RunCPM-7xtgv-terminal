"""Complete-card export and Git visibility; synthetic cards only."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from check_card_seed import card_entries
from card_paths import REPO


class CardTests(unittest.TestCase):
    def test_export_includes_drives_and_host_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'source'
            names = ('A/0/VTTEST.COM', 'A/0/7XTGVTRM.INI', 'A/0/7XTGVTRM.BUF',
                     'A/0/DRV.7XF', 'A/F/KOLEJE.DBD', 'A/F/KOLEJE.BAK',
                     'P/F/subfolder/MIXWORX.o', '.7xtgvtrm-ini-history/OLD.INI',
                     'IMG.jpg', 'README.adoc')
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'unchanged\r\n\x1a')
            (root / 'B/0').mkdir(parents=True)
            command = [sys.executable, str(TOOLS / 'check_card_seed.py'), '--root', str(root), '--export']
            out = Path(directory) / 'export'
            result = subprocess.run(command + [str(out)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name in names:
                self.assertEqual((out / name).read_bytes(), (root / name).read_bytes())
            self.assertTrue((out / 'B/0').is_dir())
            self.assertEqual(len(list(out.rglob('*'))), len(list(root.rglob('*'))))
            self.assertNotEqual(subprocess.run(command + [str(out)], capture_output=True).returncode, 0)
            self.assertNotEqual(subprocess.run(command + [str(root / 'nested')], capture_output=True).returncode, 0)
            self.assertFalse((root / 'nested').exists())

    def test_missing_drives_and_symlinks_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, 'No A-P'):
                card_entries(root)
            (root / 'A').symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'Symlink'):
                card_entries(root)
            (root / 'A').unlink()
            (root / 'A/F').mkdir(parents=True)
            (root / 'A/F/LINK').symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'Symlink'):
                card_entries(root)

    def test_git_includes_entire_card_and_overrides_generic_filters(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / '.gitignore').write_bytes((REPO / '.gitignore').read_bytes())
            names = ['cpmcard/.7xtgvtrm-ini-history/OLD.INI', 'cpmcard/IMG.jpg']
            for drive in 'ABCDEFGHIJKLMNOP':
                for leaf in ('7XTGVTRM.BUF', 'TEST.BAK', 'MIXWORX.o', 'build/a.swp', 'backups/x'):
                    names.append(f'cpmcard/{drive}/F/{leaf}')
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            result = subprocess.run(['git', 'check-ignore', '--no-index', '--stdin'], cwd=root,
                                    input='\n'.join(names)+'\n', text=True, capture_output=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            # The exception must not expose unrelated private project material.
            for name in ('docs/private/NOTE.adoc', 'docs/MIXWORX-private.adoc'):
                self.assertEqual(subprocess.run(['git', 'check-ignore', '--no-index', '-q', name], cwd=root).returncode, 0)


if __name__ == '__main__':
    unittest.main()
