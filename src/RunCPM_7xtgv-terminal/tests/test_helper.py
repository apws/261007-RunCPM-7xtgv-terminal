import configparser
import errno
import importlib.util
import os
import pathlib
import pty
import tempfile
import threading
import tty
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('bridge', pathlib.Path(__file__).parents[1] / 'tools' / 'terminal.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)

class HelperTests(unittest.TestCase):
    def test_terminal_read_hangup_is_eof_even_when_still_a_tty(self):
        for error in (errno.EIO, errno.ENXIO):
            with patch.object(bridge.os, 'isatty', return_value=True), \
                    patch.object(bridge.os, 'read', side_effect=OSError(error, 'PTY closed')):
                self.assertEqual(bridge.read_terminal_input(), b'')
        with patch.object(bridge.os, 'read', side_effect=OSError(errno.EBADF, 'bad fd')):
            with self.assertRaises(OSError):
                bridge.read_terminal_input()

    def test_transfer_ccp_prompt_forms(self):
        transfer = bridge.load_console_transfer()
        for prompt in ('A15>', 'AF>', 'A>', '/a/mbasic>', '/a/7>'):
            self.assertIsNotNone(transfer.CCP_PROMPT.fullmatch(prompt))
        for output in ('/a/overlong9>', '/other/path>', 'ordinary output>'):
            self.assertIsNone(transfer.CCP_PROMPT.fullmatch(output))

    def test_transfer_user_accepts_old_and_named_cd_outputs(self):
        transfer = bridge.load_console_transfer()
        for cd_row in (' A7:', ' A7: /a/mbasic'):
            link = object.__new__(transfer.Link)
            # A drive-only prompt needs the CD fallback. A7> already proves
            # the requested area and intentionally avoids that extra query.
            link.screen = Mock(rows=[list(cd_row), list('A>')], y=1)
            link.fresh_screen = Mock()
            link.command = Mock()
            def check(predicate, **_kwargs):
                self.assertTrue(predicate(''))
            link.until = check
            link.user(7, 'A')
            self.assertEqual(link.command.call_args_list[-1].args, ('CD',))

    def test_receive_overwrite_ync_and_target_prefill(self):
        prompt = self.file_prompt(b'r')
        path = prompt.directory/'EXIST.INI'
        path.write_bytes(b'old data')
        self.assertIsNone(prompt.feed(b'exist.ini\r'))
        self.assertEqual(prompt.stage, 'target')
        self.assertEqual(prompt.path, b'exist.ini')
        self.assertIsNone(prompt.feed(b'\r'))
        self.assertEqual(prompt.stage, 'confirm')
        self.assertIsNone(prompt.feed(b'n'))
        self.assertEqual(prompt.stage, 'target')
        self.assertIsNone(prompt.feed(b'\x15NEW.INI'))
        self.assertEqual(prompt.feed(b'\r'), ('receive','NEW.INI'))
        self.assertIsNone(prompt.overwrite)
        prompt.stage = 'target'
        self.assertIsNone(prompt.feed(b'\x15EXIST.INI\r'))
        self.assertEqual(prompt.feed(b'c'), 'cancel')
        self.assertEqual(path.read_bytes(), b'old data')

    def test_confirmed_receive_replaces_only_after_complete_get(self):
        prompt = self.file_prompt(b'r')
        path = prompt.directory/'EXIST.INI'
        path.write_bytes(b'old data')
        prompt.feed(b'EXIST.INI\r\r')
        self.assertEqual(prompt.stage,'confirm')
        self.assertEqual(prompt.feed(b'y'), ('receive','EXIST.INI'))
        module = Mock()
        link = module.Link.return_value
        link.get.side_effect = RuntimeError('failed read')
        with patch.object(bridge,'load_console_transfer', return_value=module):
            with self.assertRaisesRegex(RuntimeError,'failed read'):
                bridge.vt_transfer(123,prompt,lambda message: None)
            self.assertEqual(path.read_bytes(),b'old data')
            link.get.side_effect = None
            link.get.return_value = b'new complete data'
            bridge.vt_transfer(123,prompt,lambda message: None)
        self.assertEqual(path.read_bytes(),b'new complete data')
        self.assertEqual(list(prompt.directory.glob('.vtget-*')),[])
        # Approval for the preceding version does not cover a changed target.
        with patch.object(bridge,'load_console_transfer') as loader:
            with self.assertRaises(FileExistsError):
                bridge.vt_transfer(123,prompt,lambda message: None)
            loader.assert_not_called()

    def test_result_normalizes_newlines_and_wraps_long_error(self):
        prompt = self.file_prompt(b'r')
        prompt.stage = 'done'
        prompt.result = 'Transfer failed:\nA3>VTGET FILE.INI\nXFER ERROR\n'+'long-path/'*20
        output = prompt.screen(columns=60)
        self.assertNotIn(b'\n', output.replace(b'\r\n',b''))
        self.assertIn(b'\r\nXFER ERROR\r\n',output)
        plain = bridge.re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]',b'',output)
        self.assertTrue(all(len(line)<=59 for line in plain.split(b'\r\n')))

    def test_vt_transfer_borrows_fd_preserves_device_du_and_renames(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            folder = root/'A/F'
            folder.mkdir(parents=True)
            for direction in ('send', 'receive'):
                prompt = bridge.TransferPrompt(root=root)
                prompt.direction = direction
                prompt.select_directory('A15:')
                prompt.path = bytearray(b'VTFILE.BIN')
                prompt.source = 'VTSOURCE.BIN'
                prompt.target = 'VTTARGET.BIN'
                prompt.overwrite_approved = direction == 'send'
                payload = bytes(range(256))
                path = folder/('VTSOURCE.BIN' if direction == 'send' else 'VTTARGET.BIN')
                if direction == 'send':
                    path.write_bytes(payload)
                module = Mock()
                link = module.Link.return_value
                link.put.return_value = len(payload)
                link.get.return_value = payload
                with patch.object(bridge, 'load_console_transfer', return_value=module):
                    bridge.vt_transfer(123, prompt, lambda message: None)
                module.Link.assert_called_once_with(fd=123)
                link.require_ccp.assert_called_once()
                link.prepare.assert_called_once()
                link.user.assert_not_called()
                link.close.assert_called_once()
                if direction == 'send':
                    self.assertEqual(link.put.call_args.args[:2], ('VTTARGET.BIN', payload))
                    path.unlink()
                else:
                    self.assertEqual(link.get.call_args.args[0], 'VTSOURCE.BIN')
                    self.assertEqual(path.read_bytes(), payload)
                    with patch.object(bridge, 'load_console_transfer') as loader:
                        with self.assertRaises(FileExistsError):
                            bridge.vt_transfer(123, prompt, lambda message: None)
                        loader.assert_not_called()

    def test_vt_transfer_refuses_non_ccp_before_commands(self):
        with tempfile.TemporaryDirectory() as temporary:
            prompt = bridge.TransferPrompt(root=temporary)
            prompt.directory = pathlib.Path(temporary)
            prompt.direction = 'receive'
            prompt.du = 'A0:'
            prompt.path = bytearray(b'VTFILE.BIN')
            prompt.source = prompt.target = 'VTFILE.BIN'
            module = Mock()
            link = module.Link.return_value
            link.require_ccp.side_effect = TimeoutError()
            with patch.object(bridge, 'load_console_transfer', return_value=module):
                with self.assertRaisesRegex(RuntimeError, 'No idle CCP'):
                    bridge.vt_transfer(123, prompt, lambda message: None)
            link.prepare.assert_not_called()
            link.user.assert_not_called()
            link.get.assert_not_called()
            link.close.assert_called_once()

    def test_failed_send_and_receive_still_restore_session(self):
        with tempfile.TemporaryDirectory() as temporary:
            for direction in ('send', 'receive'):
                prompt = bridge.TransferPrompt(root=temporary)
                prompt.directory = pathlib.Path(temporary)
                prompt.direction = direction
                prompt.du = 'A0:'
                prompt.source = prompt.target = 'VTFILE.BIN'
                prompt.overwrite_approved = True
                source = prompt.directory / prompt.source
                if direction == 'send':
                    source.write_bytes(b'test')
                module = Mock()
                link = module.Link.return_value
                link.put.side_effect = link.get.side_effect = RuntimeError('transfer failed')
                with patch.object(bridge, 'load_console_transfer', return_value=module):
                    with self.assertRaisesRegex(RuntimeError, 'transfer failed'):
                        bridge.vt_transfer(123, prompt, lambda message: None)
                link.close.assert_called_once()
                if source.exists():
                    source.unlink()

    def test_konsole_profile_keeps_wheel_out_of_cpm(self):
        profile = configparser.ConfigParser()
        profile.read(pathlib.Path(__file__).parents[1] / 'tools' / '7xtgv-terminal.profile')
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
        self.assertEqual(k.feed(b'\x1db0\x1db9'), b'\x1b[>63;0v\x1b[>63;31v')
        self.assertEqual(k.feed(b'\x1db7'), b'\x1b[>63;25v')
        self.assertEqual(k.feed(b'\x1db+\x1db-'), b'\x1b[>63;33v\x1b[>63;34v')
        self.assertEqual(k.feed(b'\x1dbx'), b'\x1dbx')

    def test_zoom_host_bindings_down_in_up_out(self):
        tools = pathlib.Path(__file__).parents[1] / 'tools'
        konsole = (tools / '7xtgv-terminal.keytab').read_text()
        for key, code in [('PgDown', 66), ('PgUp', 67)]:
            lines = [line for line in konsole.splitlines()
                     if line.startswith('key ') and
                     line.split()[1:3] == [key, '+Shift-Alt+Ctrl']]
            self.assertEqual(len(lines), 1)
            self.assertTrue(lines[0].endswith(f'"\\E[>{code}v"'))
        for filename in ('kitty.conf', 'kitty-cpm.conf'):
            config = (tools / filename).read_text()
            for key, code in [('page_down', 66), ('page_up', 67)]:
                prefix = f'map ctrl+shift+{key} '
                lines = [line for line in config.splitlines() if line.startswith(prefix)]
                self.assertEqual(lines, [prefix + f'send_text all \\x1b[>{code}v'])
        for key, code in [('PgUp', 33), ('PgDown', 34)]:
            lines = [line for line in konsole.splitlines()
                     if line.startswith('key ') and
                     line.split()[1:3] == [key, '+Shift+Alt+Ctrl']]
            self.assertEqual(len(lines), 1)
            self.assertTrue(lines[0].endswith(f'"\\E[>63;{code}v"'))
        # Semantic prefix controls retain their original directions.
        keys = bridge.Keys()
        self.assertEqual(keys.feed(b'\x1d+\x1d-'), b'\x1b[>66v\x1b[>67v')

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

    def file_prompt(self, direction=b's'):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        folder = pathlib.Path(temporary.name)
        if direction == b's':
            for name in ('A.BIN', 'XILE.BAS'):
                (folder/name).write_bytes(b'test')
        prompt=bridge.TransferPrompt(root=folder)
        prompt.feed(direction)
        prompt.stage='file'
        prompt.directory=folder
        prompt.du='A0:'
        return prompt

    def test_xmodem_prompt_accepts_filename_without_forwarding_it(self):
        prompt = self.file_prompt()
        self.assertIsNone(prompt.feed(b'a.bin\r'))
        self.assertEqual(prompt.stage, 'target')
        self.assertEqual(prompt.path, b'a.bin')
        self.assertIsNone(prompt.feed(b'\r'))
        self.assertEqual(prompt.stage, 'confirm')
        self.assertIsNone(prompt.feed(b'\r'))
        self.assertEqual(prompt.feed(b'y'), ('send', 'a.bin'))

    def test_xmodem_prompt_places_cursor_at_path_end(self):
        prompt = self.file_prompt()
        prompt.feed(b'abc')
        self.assertTrue(prompt.screen().endswith(b'\x1b[3;12H'))

    def test_xmodem_both_directions_du_and_filename(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=pathlib.Path(temporary)
            folder=root/'A/6'
            folder.mkdir(parents=True)
            (folder/'EXIST.INI').write_bytes(b'test')
            for key,direction in ((b's','send'),(b'r','receive')):
                prompt=bridge.TransferPrompt(root=root)
                self.assertIsNone(prompt.feed(key+b'a6:\r'))
                self.assertEqual(prompt.stage,'file')
                self.assertEqual(prompt.directory,folder)
                self.assertEqual(prompt.path,b'')
                answer = prompt.feed(b'exist.ini\r')
                self.assertIsNone(answer)
                self.assertEqual(prompt.stage,'target')
                self.assertIsNone(prompt.feed(b'\r'))
                self.assertEqual(prompt.stage,'confirm')
                answer = prompt.feed(b'y')
                self.assertEqual(answer,(direction,'exist.ini'))
                self.assertEqual(prompt.chosen_path('exist.ini'),folder/'EXIST.INI')
                self.assertEqual(prompt.chosen_path('new.ini'),folder/'NEW.INI')
                for bad in ('../bad.ini','/tmp/bad.ini','A/0/file.ini','..'):
                    with self.assertRaises(ValueError): prompt.chosen_path(bad)

    def test_xmodem_du_missing_symlink_and_decimal_user(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=pathlib.Path(temporary)
            (root/'A/F').mkdir(parents=True)
            prompt=bridge.TransferPrompt(root=root)
            for value in ('A16:','A6:','A/0','Z0:'):
                with self.assertRaises(ValueError): prompt.select_directory(value)
            prompt.select_directory('a15:')
            self.assertEqual(prompt.du,'AF:')
            (root/'A/F').rmdir()
            (root/'A/F').symlink_to(root, target_is_directory=True)
            with self.assertRaises(ValueError): prompt.select_directory('AF:')
            prompt.feed(b'rA6:\r')
            self.assertEqual(prompt.stage,'du')
            self.assertIn('No local folder',prompt.error)

    def test_xmodem_both_directions_private_editing_fragmented(self):
        for key,direction in ((b's','send'),(b'r','receive')):
            prompt=self.file_prompt(key)
            prompt.feed(b'file.baz')
            # Exact generated Konsole Backspace event, delivered one byte at a time.
            for byte in b'\x1b[>70;1;11;0;008v':
                self.assertIsNone(prompt.feed(bytes((byte,))))
            self.assertEqual(prompt.path,b'file.ba')
            prompt.feed(b's\x1b[>30v\x1b[>34vS')
            self.assertEqual(prompt.path,b'file.baS')
            prompt.feed(b'\x1b[>32vX\x1b[>70;1;5;0;127v')
            self.assertEqual(prompt.path,b'Xile.baS')
            prompt.feed(b'\x1b[>33v\x08s')
            answer = prompt.feed(b'\x1b[>70;1;13;0;013v')
            self.assertIsNone(answer)
            self.assertEqual(prompt.stage, 'target')
            answer = prompt.feed(b'\r')
            if direction == 'send':
                self.assertIsNone(answer)
                answer = prompt.feed(b'y')
            self.assertEqual(answer, (direction,'Xile.bas'))

    def test_xmodem_ansi_editing_and_unicode(self):
        prompt=self.file_prompt()
        prompt.feed('AěB'.encode())
        prompt.feed(b'\x1b[D\x7f')
        self.assertEqual(prompt.path,b'AB')
        prompt.feed('ž'.encode()+b'\x1b[C\x08')
        self.assertEqual(prompt.path,'Až'.encode())
        prompt.feed(b'\x1b[H\x1b[3~\x1b[F.')
        self.assertIsNone(prompt.feed(b'\r'))
        self.assertIn('8.3', prompt.error)

    def test_xmodem_encoded_printable_fallback_and_unknown_sequence(self):
        prompt=self.file_prompt(b'r')
        prompt.feed(b'a\x1b[>70;1;49;0;046vb')
        self.assertEqual(prompt.path,b'a.b')
        self.assertIsNone(prompt.feed(b'\x1b[15~'))
        self.assertEqual(prompt.path,b'a.b')
        self.assertIsNone(prompt.feed(b'\x1b'))
        prompt.escape_at-=1
        self.assertEqual(prompt.feed(b''),'cancel')
        prompt=bridge.TransferPrompt()
        self.assertEqual(prompt.feed(b'\x1b[>70;1;12;0;027v'),'cancel')

    def test_xmodem_long_path_scrolls_to_cursor(self):
        prompt=self.file_prompt()
        prompt.feed(b'a'*200+b'.bas')
        screen=prompt.screen(columns=80)
        pathline=screen.split(b'Source: ',1)[1].split(b'\r',1)[0]
        self.assertTrue(pathline.endswith(b'.bas'))
        self.assertLessEqual(len(pathline),72)
        prompt.feed(b'\x1b[H')
        self.assertTrue(prompt.screen(columns=80).endswith(b'\x1b[3;9H'))

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

    def test_identity_is_hidden_with_every_split(self):
        message = b'\x1bP7xtgv-id;1;jc8048w550;260924-1259\x1b\\'
        for split in range(len(message) + 1):
            events = bridge.HostEvents()
            first, event1 = events.feed(b'before' + message[:split])
            last, event2 = events.feed(message[split:] + b'after')
            self.assertEqual(first + last, b'beforeafter')
            self.assertFalse(event1 or event2)
        events = bridge.HostEvents()
        events.feed(b'\x1bP7xtgv-id;1;' + b'x' * 200)
        self.assertLess(len(events.pending), 161)

if __name__ == '__main__':
    unittest.main()
