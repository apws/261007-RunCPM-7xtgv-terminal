"""Real CP/M helper tests; native RunCPM is NOT the embedded renderer."""
import importlib.util
import os
from pathlib import Path
import pty
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SKETCH = Path(__file__).resolve().parents[1]
ROOT = SKETCH.parents[1]
HELPERS = SKETCH / 'tools/cpm-transfer'
spec = importlib.util.spec_from_file_location('console_transfer', HELPERS/'transfer.py')
transfer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transfer)


class ConsoleTransferTests(unittest.TestCase):
    def test_profile_and_raw_restored_for_all_banks(self):
        for bank in range(10):
            for original_raw in (False, True):
                for transfer_raw in (False, True):
                    with self.subTest(bank=bank, raw=original_raw, transfer_raw=transfer_raw):
                        link = transfer.Link(fd=-1)
                        state = [bank, original_raw]
                        selected_commands = []
                        pending = [False]
                        sent = []
                        def paint(peek=False):
                            # Reproduce deployed RAW peek's effective-CCP-bank quirk.
                            shown = 0 if peek and state[1] else state[0]
                            label = f'[## RAW / #{shown}]' if state[1] else f'[#{shown} TEST]'
                            link.screen.feed(('\x1b[2J\x1b[1;1H'+label).encode())
                        def send(commands):
                            sent.append(commands)
                            for number in transfer.re.findall(r'\x1b\[>(\d+)v', commands):
                                number = int(number)
                                if 20 <= number <= 29:
                                    state[:] = [number-20, False]
                                    pending[0] = True
                                    paint()
                                elif number in (16,17):
                                    state[1] = number == 16
                                    pending[0] = False
                                    paint()
                                elif number == 14:
                                    pending[0] = False
                                    paint(peek=True)
                        def pump(seconds=.02):
                            if seconds >= 1 and pending[0]:
                                selected_commands.append(state[0])
                        link.send, link.pump = send, pump
                        link.prepare(raw=transfer_raw)
                        self.assertEqual(link.original_profile, bank)
                        self.assertEqual(state, [9, transfer_raw])
                        link.close()
                        self.assertEqual(state, [bank, original_raw])
                        self.assertEqual(selected_commands, [])
                        self.assertTrue(sent[0].startswith('\x1b[>14v'))
                        count = len(sent)
                        link.close()
                        self.assertEqual(len(sent), count)

    def test_missing_profile_aborts_before_selecting_bank_nine(self):
        link = transfer.Link(fd=-1)
        sent = []
        link.send = sent.append
        link.pump = lambda *args: None
        link.until = lambda *args, **kwargs: (_ for _ in ()).throw(TimeoutError())
        with self.assertRaises(TimeoutError):
            link.prepare()
        link.close()
        self.assertNotIn('\x1b[>29v', ''.join(sent))

    def test_restore_failure_reports_profile_and_releases_owned_fd(self):
        link = transfer.Link(fd=-1)
        link.owns_fd = True
        link.original_profile = 3
        link.profile_notice = lambda *args, **kwargs: (_ for _ in ()).throw(TimeoutError())
        with patch.object(transfer.fcntl, 'ioctl') as ioctl, patch.object(transfer.os, 'close') as close:
            with self.assertRaisesRegex(RuntimeError, 'profile #3'):
                link.close()
            ioctl.assert_called_once()
            close.assert_called_once_with(-1)

    def test_ccp_preflight_ignores_historical_transfer_error(self):
        link = transfer.Link(fd=-1)
        link.send = lambda data: None
        link.pump = lambda seconds: link.screen.feed(b'XFER ERROR\r\nA0>')
        link.require_ccp()

    def test_tagged_get_rejects_old_rows_even_if_ready_arrives_first(self):
        link = transfer.Link(fd=-1)
        def page(record, tags):
            return '\n'.join([f'R2C{record:04X}'] + [
                (f'{tag+1:02X}'*32)+f':{tag:04X}' for tag in tags
            ] + [f'READY{record:04X} FILE.BIN PCT '])
        screens = [page(0, [0]*4), page(1, [0]*4),
                   page(1, [1,0,1,0]), page(1, [1]*4), 'END0002']
        def until(predicate):
            while screens:
                text = screens.pop(0)
                if predicate(text):
                    return text
            self.fail('No complete page')
        link.until = until
        link.command = lambda command: None
        link.send = lambda data: None
        link.pump = lambda seconds: None
        self.assertEqual(link.get('VTFILE.BIN'), b'\x01'*128+b'\x02'*128)

    def test_borrowed_link_does_not_open_or_close_port(self):
        master, slave = pty.openpty()
        try:
            with patch.object(transfer.os, 'open', side_effect=AssertionError('second reader')):
                link = transfer.Link(fd=master)
                link.close()
                os.fstat(master)
        finally:
            os.close(master)
            os.close(slave)

    def test_user_switch_accepts_decimal_hex_and_drive_only_prompts(self):
        link = transfer.Link(fd=-1)
        commands = []
        for prompt in ('A10>', 'AA>', 'A>'):
            def command(text):
                commands.append(text)
                response = f'A10: /a/10\r\n{prompt}' if text == 'CD' else prompt
                link.screen.feed(response.encode())
            link.command = command
            link.user(10)
        self.assertEqual(commands, ['A10:', 'CD']*3)

    @unittest.skipUnless((ROOT/'build/linux_RunCPM/RunCPM').exists(), 'native RunCPM unavailable')
    def test_helpers_binary_roundtrip_and_single_clear(self):
        with tempfile.TemporaryDirectory(prefix='xtgv-hex-test-') as temporary:
            drive = Path(temporary)/'A/0'
            drive.mkdir(parents=True)
            for name in ('VTPUT.COM', 'VTGET.COM'):
                shutil.copyfile(HELPERS/name, drive/name)
            master, slave = pty.openpty()
            process = subprocess.Popen([str(ROOT/'build/linux_RunCPM/RunCPM')],
                cwd=temporary, stdin=slave, stdout=slave, stderr=slave,
                env=dict(os.environ, TERM='xterm'), start_new_session=True)
            os.close(slave)
            link = transfer.Link(fd=master)
            captured = bytearray()
            # Track all guest output despite Link replacing its screen per command.
            original_read = os.read
            def read(fd, size):
                data = original_read(fd, size)
                if fd == master:
                    captured.extend(data)
                return data
            try:
                with patch.object(transfer.os, 'read', side_effect=read):
                    link.until(lambda text: 'A0>' in text)
                    payload = bytes(range(256))*4 + b'Final record!'
                    padded = payload + bytes((-len(payload)) % 128)
                    captured.clear()
                    self.assertEqual(link.put('VTROUND.BIN', payload), len(padded))
                    self.assertEqual(captured.count(b'\x1b[2J'), 1)
                    self.assertEqual((drive/'VTROUND.BIN').read_bytes(), padded)
                    captured.clear()
                    self.assertEqual(link.get('VTROUND.BIN'), padded)
                    self.assertEqual(captured.count(b'\x1b[2J'), 1)
                    # Empty upload and EOF must also complete without stale markers.
                    link.put('VTEMPTY.BIN', b'')
                    self.assertEqual(link.get('VTEMPTY.BIN'), b'')
                    link.send('EXIT\r')
                    self.assertEqual(process.wait(timeout=5), 0)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
                os.close(master)


if __name__ == '__main__':
    unittest.main()
