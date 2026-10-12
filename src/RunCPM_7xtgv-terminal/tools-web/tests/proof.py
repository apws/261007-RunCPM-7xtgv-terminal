#!/usr/bin/env python3
"""Real Chromium + Wasm pthread + native Python mirror integration proof."""
import asyncio
import fcntl
import hashlib
import json
import os
from pathlib import Path
import pty
import shutil
import struct
import sys
import tempfile
import termios
import zipfile
from playwright.async_api import async_playwright

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[2]
BUILD = ROOT / 'build/RunCPM_7xtgv-terminal/desktop-web'


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file() and 'private' not in p.parts}


async def run_case(browser, root, name, card, mode='commands'):
    session = root / name
    session.mkdir()
    log = (session / 'server.log').open('wb')
    command = [sys.executable, str(HERE / 'server.py'), '--session-dir', str(session)]
    command += ['--card-zip' if card.suffix == '.zip' else '--card-root', str(card)]
    owner = await asyncio.create_subprocess_exec(*command, stdout=log, stderr=log)
    page, mirror, master, slave = None, None, None, None
    output = bytearray()
    errors = []
    console = []
    try:
        for _ in range(100):
            if (session / 'session.json').exists(): break
            if owner.returncode is not None: raise AssertionError((session / 'server.log').read_text())
            await asyncio.sleep(.1)
        info = json.loads((session / 'session.json').read_text())
        page = await browser.new_page(viewport={'width':1100, 'height':1000})
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.on('console', lambda message: console.append(message.type + ': ' + message.text))
        await page.goto(info['url'])
        await page.wait_for_function('window.webProof !== undefined', timeout=30000)
        assert await page.evaluate('crossOriginIsolated')
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 35, 100, 0, 0))
        initial = termios.tcgetattr(slave)
        os.set_blocking(master, False)
        mirror = await asyncio.create_subprocess_exec(sys.executable, str(HERE / 'mirror.py'), str(session),
                                                     stdin=slave, stdout=slave, stderr=slave)
        async def drain():
            try:
                while data := os.read(master, 65536): output.extend(data)
            except BlockingIOError: pass
            except OSError: pass
        async def until(marker, start=0, timeout=30):
            for _ in range(timeout * 20):
                await drain()
                if marker in output[start:]: return
                if mirror.returncode is not None: break
                await asyncio.sleep(.05)
            raise AssertionError(f'{name}: missing {marker!r}; tail={bytes(output[-1500:])!r}')
        async def send(command, marker, timeout=30):
            at = len(output)
            os.write(master, command)
            await until(marker, at, timeout)
            return at
        await until(b'A0>')
        await asyncio.sleep(2) # Let the initial full mirror snapshot finish.
        print(name, 'boot + native mirror PASS', flush=True)
        # Select automation bank #9 without sending a guest command.
        os.write(master, b'\x1b[>29v')
        await until(b'[#9 GENERAL]')
        await asyncio.sleep(2)
        if mode == 'commands':
            await send(b'DIR\r', b'VTHELLO')
            await send(b'VTHELLO\r', b'Browser Z80 guest works')
            await send(b'VTDRV RW\r', b'VTDRV verify: PASS', 60)
            print(name, 'guest BDOS read/write verification PASS', flush=True)
            await send(b'7XF\r', b'/a/cpm>')
            await send(b'CD /C/XTERM256\r', b'/c/xterm256>')
            await send(b'CLS\r', b'/c/xterm256>')
            # Leave room below the plot for the completion text, without a
            # full-screen text scroll repainting the retained graphics.
            os.write(master, b'\r' * 14)
            await asyncio.sleep(1)
            await send(b'SUBMIT BROT_BS\r', b'VTSTOP:', 60)
            await asyncio.sleep(.5)
            colors = await page.evaluate('''() => {
                const bytes = document.querySelector('canvas').getContext('2d').getImageData(0,0,480,320).data;
                const colors = new Set();
                for (let i=0; i<bytes.length; i+=4) colors.add((bytes[i]<<16) | (bytes[i+1]<<8) | bytes[i+2]);
                return colors.size;
            }''')
            await page.screenshot(path=str(session / 'graphics.png'))
            assert colors > 16, colors
            print(name, 'SUBMIT + Mandelbrot framebuffer PASS', flush=True)
            await send(b'CD /A/CPM\r', b'/a/cpm>')
            reboot_at = await send(b'REBOOT\r', b'RunCPM Version', 60)
            await until(b'A0>', reboot_at)
            await send(b'DIR PUN.TXT\r', b'PUN')
            print(name, 'REBOOT retained MEMFS PASS', flush=True)
            os.write(master, b'\x1dq')
        elif mode == 'import':
            await send(b'DIR PUN.TXT\r', b'PUN')
            await send(b'EXIT\r', b'EXIT')
        elif mode == 'loop':
            os.write(master, b'VTLOOP\r')
            await asyncio.sleep(1)
            os.write(master, b'\x1dq')
        elif mode == 'page-close':
            await page.close()
            page = None
        if page:
            await page.wait_for_function('window.webProof.stopped', timeout=20000)
            result = json.loads((session / 'result.json').read_text())
            assert result['finished'] == 0, result
            if mode == 'commands':
                async with page.expect_download() as download:
                    await page.click('#export')
                await (await download.value).save_as(session / 'download.zip')
                assert (session / 'download.zip').read_bytes() == (session / 'exported-card.zip').read_bytes()
                with zipfile.ZipFile(session / 'download.zip') as z:
                    assert z.read('cpmcard/A/0/PUN.TXT') == b'X'
                    assert z.read('cpmcard/A/0/7XTGVTRM.BUF')
                    assert len(z.read('cpmcard/A/0/VTDRV.$$$')) == 32768
                print(name, 'downloaded ZIP contains guest writes and history PASS', flush=True)
            await page.close()
            page = None
        await asyncio.wait_for(mirror.wait(), 15)
        assert mirror.returncode == 0
        assert termios.tcgetattr(slave) == initial, 'Native terminal mode not restored'
        await asyncio.wait_for(owner.wait(), 15)
        assert owner.returncode == 0
        assert not errors, errors
        print(name, 'lifecycle PASS', flush=True)
    finally:
        if page: await page.close()
        for child in (mirror, owner):
            if child and child.returncode is None:
                child.terminate()
                try: await asyncio.wait_for(child.wait(), 10)
                except asyncio.TimeoutError:
                    child.kill(); await child.wait()
        for fd in (master, slave):
            if fd is not None: os.close(fd)
        (session / 'native-mirror.bin').write_bytes(output)
        (session / 'browser.log').write_text('\n'.join(console + errors))
        log.close()


async def main():
    root = Path(tempfile.mkdtemp(prefix='proof-', dir=BUILD / 'sessions'))
    print('Evidence:', root, flush=True)
    card = root / 'card'
    shutil.copytree(ROOT / 'cpmcard', card, ignore=shutil.ignore_patterns('private', '.7xtgv-desktop-lock'))
    area = card / 'A/0'
    (area / 'VTHELLO.COM').write_bytes(bytes.fromhex('1110010e09cd05001e580e04cd0500c9') + b'Browser Z80 guest works\r\n$')
    (area / 'VTLOOP.COM').write_bytes(bytes.fromhex('c30001'))
    # Match the canonical empty user areas too (SUBMIT uses A/current user).
    for drive in 'ABCDEFGHIJKLMNOP':
        for user in '0123456789ABCDEF': (card / drive / user).mkdir(parents=True, exist_ok=True)
    before = hashes(card)
    async with async_playwright() as p:
        browser = await p.chromium.launch(executable_path='/usr/bin/chromium', headless=True, args=['--no-sandbox'])
        try:
            await run_case(browser, root, 'commands', card)
            await run_case(browser, root, 'import', root / 'commands/download.zip', 'import')
            await run_case(browser, root, 'loop', card, 'loop')
            await run_case(browser, root, 'page-close', card, 'page-close')
        finally: await browser.close()
    assert hashes(card) == before, 'Host source card was modified'
    (root / 'PASS').write_text('Commands, storage, graphics, reboot, export/import, infinite-loop stop, page close; source card unchanged.\n')
    print('All browser proof checks PASS; source card unchanged.', flush=True)


if __name__ == '__main__':
    asyncio.run(main())
