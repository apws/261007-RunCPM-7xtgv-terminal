#!/usr/bin/env python3
"""CP/M hex-page transfer through the interpreted VT screen (128-byte records)."""
import argparse
import fcntl
import importlib.util
import os
from pathlib import Path
import re
import select
import sys
import termios
import time

root = Path(__file__).resolve().parents[2]
if str(root / 'tools') not in sys.path:
    sys.path.insert(0, str(root / 'tools'))
from target_config import resolve_port
spec = importlib.util.spec_from_file_location('smoke', root/'tests/hardware_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)

def user_area(value):
    """Parse a CP/M mirror area written as one hexadecimal digit (0 through F)."""
    value = value.strip().upper()
    if not re.fullmatch(r'[0-9A-F]', value):
        raise argparse.ArgumentTypeError('CP/M user area must be one hexadecimal digit: 0 through F')
    return int(value, 16)

CCP_PROMPT = re.compile(
    r'(?:[A-P](?:[0-9]|1[0-5]|[A-F])?|/[a-pA-P]/[A-Za-z0-9_-]{1,8})>'
)

class Link:
    def __init__(self, port=None, raw=True, *, fd=None):
        self.owns_fd = fd is None
        self.fd = os.open(resolve_port(port), os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK) if fd is None else fd
        if self.owns_fd:
            fcntl.ioctl(self.fd, termios.TIOCEXCL)
            smoke.bridge.configure_serial(self.fd)
        self.screen = smoke.Mirror()
        self.raw = False
        self.original_profile = None
        self.original_raw = False
        self.closed = False
        if not self.owns_fd:
            return  # The mirror verifies CCP before enabling the transfer session.
        try:
            self.prepare(raw)
        except BaseException:
            self.close()
            raise

    def profile_notice(self, commands, expected=None):
        """Read the existing Alt+Space overlay, not the effective CCP bank."""
        self.pump(.05)
        self.screen = smoke.Mirror()
        self.send(commands + '\x1b[>62v')
        def notice(text):
            for row in text.splitlines():
                match = re.match(r'^\[## RAW / #([0-9])\]', row)
                if match:
                    state = (int(match[1]), True)
                else:
                    match = re.match(r'^\[#([0-9]) ', row)
                    if not match:
                        continue
                    state = (int(match[1]), False)
                if expected is None or state == expected:
                    return state
            return None
        text = self.until(lambda text: notice(text) is not None,
                          timeout=3, check_errors=False)
        return notice(text)

    def require_ccp(self):
        self.screen = smoke.Mirror()
        self.send('\x1b[>62v')
        self.pump(.5)
        self.until(lambda t: CCP_PROMPT.fullmatch(
                   ''.join(self.screen.rows[self.screen.y]).strip()) is not None,
                   timeout=3, check_errors=False)

    def prepare(self, raw=True):
        if self.original_profile is not None:
            raise RuntimeError('Transfer session already prepared')
        bank, was_raw = self.profile_notice('\x1b[>14v')
        if was_raw:
            # Older firmware's RAW peek uses the effective bank (0 at CCP).
            # RawOff peeks at selectedBank, without running on_select.
            try:
                bank, _ = self.profile_notice('\x1b[>17v\x1b[>14v')
            except BaseException:
                self.send('\x1b[>16v')
                raise
        self.original_profile, self.original_raw = bank, was_raw
        # Automated transfers use the reserved #9 history/profile bank.
        # RawOn/Off replaces the selection notice with a peek, cancelling
        # on_select even if the user configured it for bank 9.
        self.send('\x1b[>29v' + ('\x1b[>16v' if raw else '\x1b[>17v'))
        self.raw = raw
        self.pump(1.1)

    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            if self.original_profile is not None:
                state = (self.original_profile, self.original_raw)
                # One write: restore bank, then replace its on_select notice
                # with a RAW-state peek. Never launch its command/change DU.
                commands = f'\x1b[>{20 + state[0]}v' + (
                    '\x1b[>16v' if state[1] else '\x1b[>17v')
                try:
                    self.profile_notice(commands, expected=state)
                    self.pump(1.1)
                except (OSError, RuntimeError, TimeoutError) as error:
                    raise RuntimeError(
                        f'Could not confirm restored profile #{state[0]}; '
                        'check Alt+Space before continuing.') from error
                self.original_profile = None
                self.raw = state[1]
        finally:
            if self.owns_fd:
                try:
                    fcntl.ioctl(self.fd, termios.TIOCNXCL)
                finally:
                    os.close(self.fd)

    def pump(self, seconds=.02):
        end = time.monotonic()+seconds
        while time.monotonic()<end:
            if select.select([self.fd], [], [], min(.01,max(0,end-time.monotonic())))[0]:
                try:
                    data = os.read(self.fd, 8192)
                except BlockingIOError:
                    continue
                if not data: raise RuntimeError('Serial disconnected')
                self.screen.feed(data)

    def send(self, text):
        pending=text.encode('ascii')
        while pending:
            if select.select([], [self.fd], [], 1)[1]:
                pending=pending[os.write(self.fd,pending):]

    def command(self, text):
        self.send(text+'\r');self.pump(.25)

    def fresh_screen(self):
        # VTPUT/VTGET use a deterministic full-screen text protocol.  Do not
        # let an earlier failed transaction still visible in the terminal RAM
        # make the next transaction fail before its command has run.
        self.screen = smoke.Mirror()

    def until(self, predicate, timeout=15, check_errors=True):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            text=self.screen.text()
            if check_errors and 'XFER ERROR' in text:raise RuntimeError(text)
            if predicate(text):return text
            self.pump()
        raise TimeoutError(self.screen.text())

    def user(self, user, drive='A'):
        assert 0<=user<16
        assert drive in 'ABCDEFGHIJKLMNOP'
        self.fresh_screen()
        self.command(f'{drive}{user}:')
        # A verified lazy-history save temporarily pauses RunCPM. Slow SD
        # targets can therefore echo the drive/user command well before CCP
        # gets a chance to print its new prompt.
        self.until(lambda t: CCP_PROMPT.fullmatch(
            ''.join(self.screen.rows[self.screen.y]).strip()) is not None,
            timeout=60)
        # DN and drive-only prompts do not identify the numeric user area.
        # Ask the CCP itself before any transfer and verify its exact DU.
        self.fresh_screen()
        self.command('CD')
        def selected_du(_text):
            rows = [''.join(row).strip() for row in self.screen.rows]
            prefix = f'{drive}{user}: /{drive.lower()}/'
            numeric = f'{drive}{user}:'
            return (any(row == numeric or row.startswith(prefix) for row in rows)
                    and CCP_PROMPT.fullmatch(rows[self.screen.y]) is not None)
        self.until(selected_du, timeout=60)

    def get(self, name, progress=None):
        assert re.fullmatch(r'[A-Z0-9_-]{1,8}\.[A-Z0-9]{1,3}', name)
        self.fresh_screen()
        self.command('VTGET '+name)
        data=bytearray();record=0
        while True:
            marker=f'REC{record:04X}'
            tagged_marker=f'R2C{record:04X}'
            endmarker=f'END{record:04X}'
            def complete_rows(text):
                tagged = text.startswith(tagged_marker)
                if not (tagged or text.startswith(marker)) or f'READY{record:04X}' not in text:
                    return None
                rows=text.splitlines()[1:5]
                pattern = '[0-9A-F]{64}' + (f':{record:04X}' if tagged else '')
                return [r[:64] for r in rows] if len(rows)==4 and all(re.fullmatch(pattern, r) for r in rows) else None
            # Mirror cells arrive incrementally. READY can be visible before
            # the four hex rows are complete; a snapshot request in raw mode
            # would instead reach VTGET as input and abort the transfer.
            text=self.until(lambda t:endmarker in t or complete_rows(t) is not None)
            if endmarker in text:
                self.pump(.1)
                return bytes(data)
            rows=complete_rows(text)
            if rows is None:raise RuntimeError(self.screen.text())
            data.extend(bytes.fromhex(''.join(rows)))
            percent=progress.advance(128,name,'verify') if progress else None
            enhanced='PCT ' in text
            self.send((f'{percent:03d}%' if enhanced and percent is not None else '')+'\r');record+=1

    def put(self, name, data, progress=None, verify=False):
        assert re.fullmatch(r'[A-Z0-9_-]{1,8}\.[A-Z0-9]{1,3}', name)
        data=data+bytes((-len(data))%128)
        self.fresh_screen()
        self.command('VTPUT '+name)
        for count,offset in enumerate(range(0,len(data),32)):
            marker=f'PUT{count:04X}:'
            screen=self.until(lambda t:t.startswith(marker))
            percent=progress.advance(32,name,'write') if progress else None
            suffix=f':{percent:03d}' if '%' in screen and percent is not None else ''
            self.send(data[offset:offset+32].hex().upper()+suffix+'\r')
        marker=f'PUT{len(data)//32:04X}:'
        self.until(lambda t:t.startswith(marker))
        self.send('!\r');self.until(lambda t:'SAVED' in t);self.pump(.2)
        if verify:
            actual=self.get(name, progress)
            if actual != data:
                raise RuntimeError('Read-back mismatch: ' + name)
        return len(data)

class Progress:
    def __init__(self,total):
        self.total=max(total,1);self.done=0;self.last=-1;self.line_open=False
    def advance(self,count,name,phase):
        self.done=min(self.total,self.done+count)
        percent=self.done*100//self.total
        if percent!=self.last:
            width=20;filled=percent*width//100
            sys.stdout.write(f'\r[{"#"*filled}{"."*(width-filled)}] {percent:3d}% {name} {phase}')
            sys.stdout.flush();self.last=percent;self.line_open=True
        return percent
    def newline(self):
        if self.line_open:
            sys.stdout.write('\n');sys.stdout.flush();self.line_open=False

def cpm_text(data):
    """Convert host text into CP/M CR/LF text ending with Ctrl+Z."""
    # A file previously edited on CP/M may contain whole-record NUL padding
    # after its logical Ctrl-Z.  Never transfer that tail as text content.
    data=data.split(b'\x1a',1)[0]
    data=data.replace(b'\r\n',b'\n').replace(b'\r',b'\n')
    return data.replace(b'\n',b'\r\n')+b'\x1a'

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',help='Serial device; otherwise XTGV_PORT or selected target data')
    p.add_argument('--target',help='Target for port defaults; otherwise XTGV_TARGET')
    p.add_argument('--user',type=user_area,default=0,metavar='0-F',
                   help='CP/M mirror user area in hexadecimal (default: 0)')
    p.add_argument('--no-raw',action='store_true',
                   help='keep normal #9 mapped input for comparison testing')
    p.add_argument('--verify', action='store_true',
                   help='read uploaded files back for byte-for-byte verification')
    p.add_argument('--cpm-text', action='store_true',
                   help='convert uploaded host text to CP/M CR/LF text ending in Ctrl+Z')
    p.add_argument('action',choices=['get','put'])
    p.add_argument('files',nargs='+')
    p.add_argument('--directory',type=Path,default=Path('.'))
    args=p.parse_args()
    try:
        args.port = resolve_port(args.port, args.target)
    except RuntimeError as error:
        p.error(str(error))
    paths=[Path(f) for f in args.files]
    progress=None; uploadData={}
    if args.action=='put':
        for path in paths:
            data=path.read_bytes()
            uploadData[path]=cpm_text(data) if args.cpm_text else data
        # Reserve a second equal pass only for an explicit byte-for-byte
        # read-back.  A write-only upload must visibly finish at 100%.
        passes=2 if args.verify else 1
        total=sum(((len(uploadData[path])+127)//128)*128*passes for path in paths)
        progress=Progress(total)
    link=Link(args.port,raw=not args.no_raw)
    try:
        link.user(args.user)
        for f,path in zip(args.files,paths):
            if args.action=='get':
                data=link.get(f.upper());args.directory.mkdir(parents=True,exist_ok=True)
                (args.directory/f.upper()).write_bytes(data)
                print('READ',f,len(data),flush=True)
            else:
                size=link.put(path.name.upper(),uploadData[path],progress,verify=args.verify)
                if progress:progress.newline()
                print('VERIFIED' if args.verify else 'SAVED',path.name,size,flush=True)
    finally:link.close()
