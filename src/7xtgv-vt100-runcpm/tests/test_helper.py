import configparser
import importlib.util
import os
import pathlib
import pty
import tempfile
import threading
import tty
import unittest

spec = importlib.util.spec_from_file_location('bridge', pathlib.Path(__file__).parents[1] / 'tools' / 'terminal.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)

class HelperTests(unittest.TestCase):
    def test_konsole_profile_keeps_wheel_out_of_cpm(self):
        profile = configparser.ConfigParser()
        profile.read(pathlib.Path(__file__).parents[1] / 'tools' / '7xtgv-cpm.profile')
        self.assertFalse(profile.getboolean('Interaction Options', 'AllowMouseTracking'))
        self.assertFalse(profile.getboolean('Interaction Options', 'AlternateScrolling'))

    def test_mirror_resets_host_mouse_and_alternate_scroll_modes(self):
        resets = bridge.mirror_sanitize_host_modes()
        for mode in bridge.MOUSE_MODES:
            self.assertIn(f'\x1b[?{mode}l'.encode(), resets)
        self.assertTrue(bridge.mirror_init().startswith(b'\x1b[?1049h' + resets))

    def test_mouse_reports_are_discarded_without_filtering_keyboard_csi(self):
        reports = bridge.MouseReports()
        self.assertEqual(reports.feed(b'\x1b[<64;12;'), b'')
        self.assertEqual(reports.feed(b'34M\x1b[A\x1b[H\x1b[F'),
                         b'\x1b[A\x1b[H\x1b[F')
        self.assertEqual(reports.feed(b'\x1b[M`23\x1b[96;12;34M'), b'')
        self.assertEqual(reports.feed(b'\x1b[<0;1;2m\x1b[1;5D\x1b[200~'),
                         b'\x1b[1;5D\x1b[200~')
        self.assertEqual(reports.feed(b'\x1b[<64;12;'), b'')
        reports.pending_at = 0.0
        self.assertEqual(reports.tick(), b'\x1b[<64;12;')

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
        self.assertEqual(k.feed(b'\x1db7'), b'\x1b[>63;25v')
        self.assertEqual(k.feed(b'\x1db+\x1db-'), b'\x1b[>63;33v\x1b[>63;34v')
        self.assertEqual(k.feed(b'\x1dbx'), b'\x1dbx')

    def test_literal_and_unknown(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'\x1d\x1d\x1dz'), b'\x1d\x1dz')

    def test_xmodem_prefix_is_host_only(self):
        k = bridge.Keys()
        self.assertEqual(k.feed(b'\x1dm'), b'')
        self.assertTrue(k.xmodem)

    def test_xmodem_crc_packet(self):
        payload = b'123456789' + b'\x1a' * 119
        self.assertEqual(bridge.xmodem_crc(b'123456789'), 0x31C3)
        packet = bridge.xmodem_packet(1, payload)
        self.assertEqual(len(packet), 133)
        self.assertEqual(packet[:3], b'\x01\x01\xfe')
        self.assertEqual(packet[3:131], payload)
        self.assertEqual(packet[131:], bridge.xmodem_crc(payload).to_bytes(2, 'big'))

    def test_xmodem_prompt_accepts_path_without_forwarding_it(self):
        prompt = bridge.XmodemPrompt()
        self.assertIsNone(prompt.feed(b's'))
        self.assertEqual(prompt.feed(b'/tmp/a.bin\r'), ('send', '/tmp/a.bin'))

    def test_xmodem_prompt_places_cursor_at_path_end(self):
        prompt = bridge.XmodemPrompt()
        prompt.feed(b'sabc')
        self.assertTrue(prompt.screen().endswith(b'\x1b[3;10H'))

    def test_xmodem_sends_crc_blocks(self):
        peer, ours = pty.openpty()
        tty.setraw(ours)
        received = bytearray()

        def read_exact(size):
            result = bytearray()
            while len(result) < size:
                result.extend(os.read(peer, size - len(result)))
            return bytes(result)

        def receiver():
            os.write(peer, b'C')
            received.extend(read_exact(133))
            os.write(peer, b'\x06')
            self.assertEqual(read_exact(1), b'\x04')
            os.write(peer, b'\x06')

        thread = threading.Thread(target=receiver)
        thread.start()
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'source.bin'
            source.write_bytes(b'hello')
            self.assertEqual(bridge.Xmodem(ours, lambda message: None).send(source), 5)
        thread.join(2)
        self.assertFalse(thread.is_alive())
        os.close(ours); os.close(peer)
        self.assertEqual(received, bridge.xmodem_packet(1, b'hello'.ljust(128, b'\x1a')))

    def test_xmodem_receives_crc_blocks_without_overwrite(self):
        peer, ours = pty.openpty()
        tty.setraw(ours)

        def read_exact(size):
            result = bytearray()
            while len(result) < size:
                result.extend(os.read(peer, size - len(result)))
            return bytes(result)

        def sender():
            self.assertEqual(read_exact(1), b'C')
            os.write(peer, bridge.xmodem_packet(1, b'world'.ljust(128, b'\x1a')))
            self.assertEqual(read_exact(1), b'\x06')
            os.write(peer, b'\x04')
            self.assertEqual(read_exact(1), b'\x06')

        thread = threading.Thread(target=sender)
        thread.start()
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory) / 'destination.bin'
            self.assertEqual(bridge.Xmodem(ours, lambda message: None).receive(destination), 128)
            self.assertEqual(destination.read_bytes(), b'world'.ljust(128, b'\x1a'))
            with self.assertRaises(FileExistsError):
                bridge.Xmodem(ours, lambda message: None).receive(destination)
        thread.join(2)
        self.assertFalse(thread.is_alive())
        os.close(ours); os.close(peer)

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
