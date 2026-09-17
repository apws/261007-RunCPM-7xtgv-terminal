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
spec = importlib.util.spec_from_file_location('smoke', root/'tests/hardware_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)

def user_area(value):
    """Parse a CP/M mirror area written as one hexadecimal digit (0 through F)."""
    value = value.strip().upper()
    if not re.fullmatch(r'[0-9A-F]', value):
        raise argparse.ArgumentTypeError('CP/M user area must be one hexadecimal digit: 0 through F')
    return int(value, 16)

class Link:
    def __init__(self, port='/dev/ttyACM0', raw=True):
        self.fd = os.open(port, os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
        fcntl.ioctl(self.fd, termios.TIOCEXCL)
        smoke.bridge.configure_serial(self.fd)
        self.screen = smoke.Mirror()
        self.send('\x1b[>0v')
        self.pump(.2)
        # Automated transfers use the reserved #9 history/profile bank.
        self.send('\x1b[>29v')
        self.pump(1.1)
        self.raw = raw
        if raw:
            self.send('\x1b[>16v')
            self.pump(.1)

    def close(self):
        if self.raw:
            self.send('\x1b[>17v')
            self.pump(.1)
        fcntl.ioctl(self.fd, termios.TIOCNXCL)
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

    def until(self, predicate, timeout=15):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            text=self.screen.text()
            if 'XFER ERROR' in text:raise RuntimeError(text)
            if predicate(text):return text
            self.pump()
        raise TimeoutError(self.screen.text())

    def user(self, user):
        assert 0<=user<16
        self.fresh_screen()
        self.command(f'A{user}:')
        # A verified lazy-history save temporarily pauses RunCPM. Slow SD
        # targets can therefore echo the drive/user command well before CCP
        # gets a chance to print its new prompt.
        self.until(lambda t:f'A{user}>' in t, timeout=60)

    def get(self, name, progress=None):
        assert re.fullmatch(r'[A-Z0-9_-]{1,8}\.[A-Z0-9]{1,3}', name)
        self.fresh_screen()
        self.command('VTGET '+name)
        data=bytearray();record=0
        while True:
            marker=f'REC{record:04X}'
            endmarker=f'END{record:04X}'
            text=self.until(lambda t:endmarker in t or (t.startswith(marker) and f'READY{record:04X}' in t))
            if endmarker in text:
                self.pump(.1)
                return bytes(data)
            rows=text.splitlines()[1:5]
            if len(rows)!=4 or any(not re.fullmatch('[0-9A-F]{64}', r) for r in rows):
                self.send('\x1b[>0v');self.pump(.15)
                rows=self.screen.text().splitlines()[1:5]
            if len(rows)!=4 or any(not re.fullmatch('[0-9A-F]{64}', r) for r in rows):raise RuntimeError(self.screen.text())
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
    p.add_argument('--port',default='/dev/ttyACM0')
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
