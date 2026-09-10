import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location('bridge', pathlib.Path(__file__).parents[1] / 'tools' / 'terminal.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)

class HelperTests(unittest.TestCase):
    def test_ordinary_control_bytes(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'DIR\r\x03\x1b[A'), b'DIR\r\x03\x1b[A')

    def test_czech_alt_number_row_selects_banks(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'\x1b+'), b'\x1b[>21v')
        self.assertEqual(k.feed(b'\x1b' + 'ě'.encode()), b'\x1b[>22v')
        self.assertEqual(k.feed(b'\x1b' + 'é'.encode()), b'\x1b[>20v')

    def test_alt_space_shows_active_bank(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'\x1b '), b'\x1b[>14v')

    def test_raw_toggle_shortcuts(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'\x1b#'), b'\x1b[>15v')
        self.assertEqual(k.feed(b'\x1d#'), b'\x1b[>15v')

    def test_fragmented_prefix(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'\x1d'), b'')
        self.assertEqual(k.feed(b'2'), b'\x1b[>2v')
        self.assertEqual(k.feed(b'\x1da\x1de\x1dh\x1dl'), b'\x1b[1;3H\x1b[1;3F\x1b[1;3D\x1b[1;3C')

    def test_literal_and_unknown(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'\x1d\x1d\x1dz'), b'\x1d\x1dz')

    def test_quit_does_not_reach_cpm(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'X\x1dqUNSENT'), b'X')
        self.assertTrue(k.quit)

if __name__ == '__main__':
    unittest.main()
