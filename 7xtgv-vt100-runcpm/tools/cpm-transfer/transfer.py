#!/usr/bin/env python3
"""CP/M hex-page transfer through the interpreted VT screen (128-byte records)."""
import argparse
import fcntl
import importlib.util
import os
from pathlib import Path
import re
import select
import termios
import time

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('smoke', root/'tests/hardware_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)

class Link:
    def __init__(self, port='/dev/ttyACM0'):
        self.fd = os.open(port, os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
        fcntl.ioctl(self.fd, termios.TIOCEXCL)
        smoke.bridge.configure_serial(self.fd)
        self.screen = smoke.Mirror()
        self.send('\x1b[>0v')
        self.pump(.2)

    def close(self):
        fcntl.ioctl(self.fd, termios.TIOCNXCL)
        os.close(self.fd)

    def pump(self, seconds=.02):
        end = time.monotonic()+seconds
        while time.monotonic()<end:
            if select.select([self.fd], [], [], min(.01,max(0,end-time.monotonic())))[0]:
                data = os.read(self.fd, 8192)
                if not data: raise RuntimeError('Serial disconnected')
                self.screen.feed(data)

    def send(self, text):
        pending=text.encode('ascii')
        while pending:
            if select.select([], [self.fd], [], 1)[1]:
                pending=pending[os.write(self.fd,pending):]

    def command(self, text):
        self.send(text+'\r');self.pump(.25)

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
        self.command(f'A{user}:')
        self.until(lambda t:f'A{user}>' in t)

    def get(self, name):
        assert re.fullmatch(r'[A-Z0-9_-]{1,8}\.[A-Z0-9]{1,3}', name)
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
            self.send('\r');record+=1

    def put(self, name, data):
        assert re.fullmatch(r'[A-Z0-9_-]{1,8}\.[A-Z0-9]{1,3}', name)
        data=data+bytes((-len(data))%128)
        self.command('VTPUT '+name)
        for count,offset in enumerate(range(0,len(data),32)):
            marker=f'PUT{count:04X}:'
            self.until(lambda t:t.startswith(marker))
            self.send(data[offset:offset+32].hex().upper()+'\r')
        marker=f'PUT{len(data)//32:04X}:'
        self.until(lambda t:t.startswith(marker))
        self.send('!\r');self.until(lambda t:'SAVED' in t);self.pump(.2)
        actual=self.get(name)
        if actual!=data:raise RuntimeError('Read-back mismatch: '+name)
        return len(data)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',default='/dev/ttyACM0')
    p.add_argument('--user',type=int,default=0)
    p.add_argument('action',choices=['get','put'])
    p.add_argument('files',nargs='+')
    p.add_argument('--directory',type=Path,default=Path('.'))
    args=p.parse_args();link=Link(args.port)
    try:
        link.user(args.user)
        for f in args.files:
            if args.action=='get':
                data=link.get(f.upper());args.directory.mkdir(parents=True,exist_ok=True)
                (args.directory/f.upper()).write_bytes(data)
                print('READ',f,len(data),flush=True)
            else:
                path=Path(f);print('VERIFIED',path.name,link.put(path.name.upper(),path.read_bytes()),flush=True)
    finally:link.close()
