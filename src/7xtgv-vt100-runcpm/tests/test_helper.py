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
        self.assertEqual(k.feed(b'\x1dg'), b'\x1b[>4v')
        self.assertEqual(k.feed(b'\x1da\x1de\x1dh\x1dl'), b'\x1b[1;3H\x1b[1;3F')

    def test_backlight_prefix(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'\x1db7'), b'\x1b[>63;7v')
        self.assertEqual(k.feed(b'\x1db+\x1db-'), b'\x1b[>63;10v\x1b[>63;11v')
        self.assertEqual(k.feed(b'\x1dbx'), b'\x1dbx')

    def test_literal_and_unknown(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'\x1d\x1d\x1dz'), b'\x1d\x1dz')

    def test_quit_does_not_reach_cpm(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'X\x1dqUNSENT'), b'X')
        self.assertTrue(k.quit)

    def test_guest_reflash_event_is_consumed_as_an_event(self):
        events = bridge.HostEvents()
        self.assertEqual(events.feed(b'normal\x1b[>'), (b'normal', False))
        self.assertEqual(events.feed(b'64vafter'), (b'after', True))

    def test_non_event_escape_sequence_is_preserved(self):
        events = bridge.HostEvents()
        self.assertEqual(events.feed(b'\x1b[31mOK'), (b'\x1b[31mOK', False))

if __name__ == '__main__':
    unittest.main()
