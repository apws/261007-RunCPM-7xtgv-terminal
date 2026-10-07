"""Canonical DU layout and explicit root selection; no live card writes."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import card_paths


class CardPathsTests(unittest.TestCase):
    def test_default_and_overrides(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertEqual(card_paths.card_root(), card_paths.REPO / 'cpmcard')
            self.assertEqual(card_paths.transfer_root(), card_paths.card_root())
        with patch.dict(os.environ, {'XTGV_CARD_ROOT': '/tmp/working-card',
                                    'XTGV_TRANSFER_ROOT': '/tmp/transfer-card'}):
            self.assertEqual(card_paths.card_root(), Path('/tmp/working-card'))
            self.assertEqual(card_paths.transfer_root(), Path('/tmp/transfer-card'))

    def test_every_hex_area_and_decimal_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for number in range(16):
                area = root / 'A' / format(number, 'X')
                area.mkdir(parents=True)
                expected = (f'A{number:X}:', area)
                self.assertEqual(card_paths.du_directory(root, f'a{number:x}:'), expected)
                if number >= 10:
                    self.assertEqual(card_paths.du_directory(root, f'A{number}:'), expected)

    def test_no_old_layout_fallback_or_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'A0 - old').mkdir()
            with self.assertRaisesRegex(ValueError, 'No local folder'):
                card_paths.du_directory(root, 'A0')
            (root / 'A').symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'symlinks'):
                card_paths.du_directory(root, 'A0')
            for bad in ('A16:', 'A/0', '../', 'Z0', ''):
                with self.assertRaises(ValueError):
                    card_paths.du_directory(root, bad)

    def test_missing_fixture_is_actionable(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(SystemExit, 'Provision.*XTGV_CARD_ROOT'):
                card_paths.require_files(Path(directory) / 'MBASIC.COM')


if __name__ == '__main__':
    unittest.main()
