"""Owner coordination and Mintty protocol contracts; no GUI qualification."""
from pathlib import Path
from unittest.mock import patch
import sys
import io
import contextlib
import os
import re
import tempfile
import unittest

SKETCH = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SKETCH / 'tools-win'))
from mintty_profile import bindings, write_profile
from mintty_keys import MinttyInput, decode
from desktop_card_lock import LOCK_NAME, card_lock
from key_event_ids import key_ids
from terminal import Keys, TransferPrompt


class WindowsMirrorTools(unittest.TestCase):
    def test_launch_paths_go_to_adjacent_log(self):
        import argparse
        import windows_run
        with tempfile.TemporaryDirectory(prefix='launch with spaces ') as temp:
            root = Path(temp)
            (root/'cpmcard/A/0').mkdir(parents=True)
            build = root/'build'; build.mkdir()
            args = argparse.Namespace(card_root=None, card_mode='local', window_scale=2,
                                      log=root/'desktop-local.log')
            def started(command, **kwargs):
                directory = Path(command[command.index('--session-dir') + 1])
                windows_run.status(directory, 'running')
            screen = io.StringIO()
            with patch.object(windows_run, 'REPO', root), patch.object(windows_run, 'require_msys'), \
                    patch.object(Path, 'is_file', return_value=True), \
                    patch.object(windows_run, 'write_profile'), \
                    patch.object(windows_run.subprocess, 'Popen', side_effect=started), \
                    contextlib.redirect_stdout(screen):
                windows_run.launch(args, 'jc8048w550', build)
            self.assertNotIn(temp, screen.getvalue())
            self.assertIn('Desktop and mirror: running.', screen.getvalue())
            self.assertIn('Card: ' + str(root/'cpmcard'), args.log.read_text())
            self.assertIn('Session logs: ' + str(build/'sessions'), args.log.read_text())

    def test_adjacent_transcript_preserves_bootstrap_and_records_failure(self):
        from shortcut_log import shortcut_log, append_log
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / 'desktop-local.log'
            log.write_text('Bootstrap MSYS2\n')
            screen = io.StringIO()
            with patch.dict(os.environ, {'XTGV_SHORTCUT_LOG': str(log)}), \
                    contextlib.redirect_stdout(screen):
                with self.assertRaisesRegex(RuntimeError, 'fixture failure'):
                    with shortcut_log(log):
                        print('Launching desktop ...')
                        append_log(log, 'Embedded build: fixture\n')
                        raise RuntimeError('fixture failure')
            self.assertEqual(screen.getvalue(), 'Launching desktop ...\n')
            data = log.read_text()
            for item in ('Bootstrap MSYS2', 'Launching desktop ...', 'Embedded build: fixture', 'FAILED: fixture failure'):
                self.assertIn(item, data)

    def test_window_scale_reaches_native_child(self):
        import windows_run
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with patch.object(windows_run.subprocess, 'Popen') as child, \
                    patch.object(windows_run.threading, 'Thread'):
                process = windows_run.DesktopProcess(root / 'desktop', root, root, False, 3)
                command = child.call_args.args[0]
                self.assertEqual(command[command.index('--window-scale') + 1], '3')
                process.log.close()

    def test_card_lock_excludes_second_writer_and_releases_after_error(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaisesRegex(ValueError, 'fixture'):
                with card_lock(root):
                    with self.assertRaisesRegex(RuntimeError, 'already locked'):
                        with card_lock(root):
                            self.fail('Second owner entered')
                    raise ValueError('fixture')
            self.assertFalse((root / LOCK_NAME).exists())
            with card_lock(root):
                self.assertTrue((root / LOCK_NAME / 'owner.json').is_file())

    def test_retained_lock_blocks_relaunch_after_unresponsive_child(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with card_lock(root) as lease:
                lease.retain()
            with self.assertRaisesRegex(RuntimeError, 'already locked'):
                with card_lock(root):
                    self.fail('Retained lock was ignored')

    def test_mintty_actions_preserve_global_controls(self):
        rules = bindings()
        for chord, data in {'A+Back': b'\x1b[>61v',
                            'CS+Back': b'\x1b[>65v', 'CS+Prior': b'\x1b[>67v',
                            'CSA+Next': b'\x1b[>63;34v'}.items():
            self.assertEqual(Keys().feed(rules[chord][1:-1].encode()), data)
        self.assertEqual(decode(f'\x1b[{rules["CA+c"]};7~'.encode()),
                         f'\x1b[>70;1;{key_ids()["KeyC"]};5;v'.encode())
        self.assertEqual(rules['F11'], 'fullscreen')
        self.assertEqual(rules['A+F4'], 'close')

    def test_event_keys_edit_transfer_panel_without_guest_profile(self):
        rules = bindings()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'A/0').mkdir(parents=True)
            prompt = TransferPrompt(root)
            prompt.feed(b'sA0\rABX')
            prompt.feed(rules['Back'][1:-1].encode())
            prompt.feed(rules['Home'][1:-1].encode())
            prompt.feed(b'C')
            self.assertEqual(prompt.path, b'CAB')
            self.assertEqual(prompt.feed(rules['Esc'][1:-1].encode()), 'cancel')

    def test_alt_page_keys_select_banks_like_linux_mirrors(self):
        rules = bindings()
        konsole = (SKETCH / 'tools/7xtgv-terminal.keytab').read_text()
        kitty = (SKETCH / 'tools/kitty.conf').read_text()
        for mintty, qt, kitty_key, control in (
                ('Prior', 'PgUp', 'page_up', 12), ('Next', 'PgDown', 'page_down', 13)):
            expected = f'\x1b[>{control}v'.encode()
            for keypad in ('', 'KP_'):
                self.assertEqual(Keys().feed(rules[f'A+{keypad}{mintty}'][1:-1].encode()), expected)
            self.assertIn(f'key {qt:<10}-Shift+Alt-Ctrl : "\\E[>{control}v"', konsole)
            self.assertIn(f'map alt+{kitty_key} send_text all \\x1b[>{control}v', kitty)

    def test_brightness_presets_match_linux_for_digits_and_czech_row(self):
        rules = bindings()
        for digit, czech, level in zip('123456789', '+ěščřžýáí', (4, 7, 11, 14, 18, 21, 25, 28, 31)):
            for key in (digit, czech):
                self.assertEqual(decode(f'\x1b[{rules[key]};6~'.encode()), f'\x1b[>63;{level}v'.encode())
        for key in ('A+0', 'A+é'):
            self.assertEqual(decode(f'\x1b[{rules[key]};8~'.encode()), b'\x1b[>63;0v')
        self.assertEqual(decode(b'\x1b[900;3~'), b'\x1b[>20v')
        self.assertNotEqual(decode(b'\x1b[900;6~'), b'\x1b[>63;0v')

    def test_mintty_numeric_modifiers_and_stream_boundaries(self):
        for packet, expected in ((b'\x1b[910;3~', b'\x1b[>60v'),
                                 (b'\x1b[910;5~', b'\x11'),
                                 (b'\x1b[912;3~', b'\x1bc'),
                                 (b'\x1b[901;8~', b'\x1b[>1v'),
                                 (b'\x1b[902;8~', b'\x1b[>2v'),
                                 (b'\x1b[903;4~', b'\x1b[>15v'),
                                 (b'\x1b[911;6~', b'\x1dm'),
                                 (b'\x1b[911;5~', b'\x18'),
                                 (b'\x1b[999;8~', b'\x1b[999;8~')):
            for split in range(len(packet) + 1):
                decoder = MinttyInput()
                self.assertEqual(decoder.feed(packet[:split]) + decoder.feed(packet[split:]), expected)
        decoder = MinttyInput()
        self.assertEqual(decoder.feed('ordinary ěšč'.encode() + b'\x1b[A'), 'ordinary ěšč'.encode() + b'\x1b[A')
        self.assertEqual(decoder.feed(b'\x1b'), b'')
        decoder.at -= 1
        self.assertEqual(decoder.tick(), b'\x1b')
        keys = Keys()
        self.assertEqual(keys.feed(decode(b'\x1b[911;6~')), b'')
        self.assertTrue(keys.xmodem)

    def test_numlock_shift_removed_keypad_bindings(self):
        rules = bindings()
        for digit, name in enumerate(('Insert', 'End', 'Down', 'Next', 'Left',
                                      'Begin', 'Right', 'Home', 'Up', 'Prior')):
            if digit:
                level = (0, 4, 7, 11, 14, 18, 21, 25, 28, 31)[digit]
                for mods in ('C', 'CS'):
                    self.assertEqual(rules[f'{mods}+KP_{name}'], f'"\x1b[>63;{level}v"')
            if digit <= 2:
                expected = b'\x1b[>63;0v' if digit == 0 else f'\x1b[>{digit}v'.encode()
                for mods in ('CA', 'CSA'):
                    self.assertEqual(Keys().feed(rules[f'{mods}+KP_{name}'][1:-1].encode()), expected)
        # Dedicated PageUp/Down still zoom, keypad positions 9/3 select levels.
        self.assertEqual(rules['CS+Prior'], '"\x1b[>67v"')
        self.assertEqual(rules['CS+KP_Prior'], '"\x1b[>63;31v"')
        self.assertEqual(rules['CS+Next'], '"\x1b[>66v"')
        self.assertEqual(rules['CSA+Prior'], '"\x1b[>63;33v"')

    def test_native_keypad_reports_and_digit_fallbacks(self):
        # Exact native probe regressions: OFF, ANSI, VT52 and brightness 6.
        captured = ((b'\x1b[1;8p', b'\x1b[>63;0v'),
                    (b'\x1b[1;8q', b'\x1b[>1v'),
                    (b'\x1b[1;8r', b'\x1b[>2v'),
                    (b'\x1b[1;6v', b'\x1b[>63;21v'))
        from keyboard_probe import translate_capture, cases
        for wire, expected in captured:
            self.assertEqual(translate_capture(wire), expected)
        for digit, level in enumerate((4, 7, 11, 14, 18, 21, 25, 28, 31), 1):
            expected = f'\x1b[>63;{level}v'.encode()
            for wire in (f'\x1b[1;6{chr(112 + digit)}'.encode(),
                         f'\x1b[27;6;{48 + digit}~'.encode(),
                         f'\x1b[{48 + digit};6u'.encode()):
                for split in range(len(wire) + 1):
                    decoder = MinttyInput()
                    actual = decoder.feed(wire[:split]) + decoder.feed(wire[split:])
                    self.assertEqual(Keys().feed(actual), expected)
        for wire in (b'123', b'\x1bOp', b'\x1b[1;5v', b'\x1b[1;6p',
                     b'\x1b[1;8s', b'\x1b[1;6A', b'\x1b[27;7;48~',
                     b'\x1b[>70;1;1;3;027091049059054068v', b'\x1b[48;5u'):
            self.assertEqual(MinttyInput().feed(wire), wire)
        self.assertEqual(translate_capture(b'\x1b#'), b'\x1b[>15v')
        self.assertEqual(translate_capture(b'\x1b[911;6~'), b'\x1dm')
        self.assertEqual(len(list(cases('top-row'))), 12)
        self.assertEqual(len(list(cases('numpad'))), 12)
        decoder = MinttyInput()
        self.assertEqual(decoder.feed(b'\x1b[1;8'), b'')
        decoder.at -= 1
        self.assertEqual(decoder.tick(), b'\x1b[1;8')

    def test_windows_global_controls_match_canonical_konsole_bytes(self):
        source = (SKETCH / 'tools/7xtgv-terminal.keytab').read_text()
        cases = [('0', 7, b'\x1b[900;8~'), ('1', 7, b'\x1b[901;8~'),
                 ('2', 7, b'\x1b[902;8~'), ('3', 6, b'\x1b[903;4~'),
                 ('Q', 4, b'\x1b[910;3~'), ('X', 3, b'\x1b[911;6~')]
        cases += [(str(d), 4, f'\x1b[{900+d};3~'.encode()) for d in range(10)]
        cases += [(str(d), 3, f'\x1b[{900+d};6~'.encode()) for d in range(1, 10)]
        for key, mods, packet in cases:
            flags = ''.join(('+' if mods & bit else '-') + name
                            for bit, name in ((2, 'Shift'), (4, 'Alt'), (1, 'Ctrl')))
            match = re.search(r'^key\s+' + re.escape(key) + r'\s+' + re.escape(flags) + r' : "([^"\n]+)"', source, re.M)
            self.assertIsNotNone(match, (key, flags))
            expected = match[1].replace('\\E', '\x1b').replace('\\x1d', '\x1d').encode()
            self.assertEqual(decode(packet), expected)

    def test_exit_alias_in_all_profiles(self):
        self.assertEqual(decode(f'\x1b[{bindings()["A+q"]};3~'.encode()), b'\x1b[>60v')
        konsole = (SKETCH / 'tools/7xtgv-terminal.keytab').read_text()
        self.assertIn('key Q          -Shift+Alt-Ctrl : "\\E[>60v"', konsole)
        self.assertNotIn('key Q          -Shift+Alt-Ctrl-KeyPad', konsole)
        for name in ('kitty.conf', 'kitty-cpm.conf'):
            self.assertIn('map alt+q send_text all \\x1b[>60v', (SKETCH / 'tools' / name).read_text())


    def test_config_contains_literal_protocol_and_modified_layout_aliases(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'mintty.conf'
            write_profile(path)
            data = path.read_bytes()
            self.assertIn(b'KeyFunctions=\x1f', data)
            self.assertIn(b'\x1b[>70;1;', data)
            self.assertNotIn(b'\r', data)
            self.assertNotIn(b'\0', data)
            self.assertIn('ě:'.encode(), data)
        # Space controls encode NUL as decimal digits, not a config-file NUL.
        self.assertIn(';000v', bindings()['C+Space'])


if __name__ == '__main__':
    unittest.main()
