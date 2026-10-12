#!/usr/bin/env python3
"""Exercise browser events, real C++/Wasm, static hosting and card round trips."""
import asyncio
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import zipfile
from playwright.async_api import async_playwright
from proof import hashes, ROOT, HERE, BUILD


async def main():
    root = Path(tempfile.mkdtemp(prefix='standalone-proof-', dir=BUILD / 'sessions'))
    print('Evidence:', root, flush=True)
    card = root / 'card'
    shutil.copytree(ROOT / 'cpmcard', card, ignore=shutil.ignore_patterns('private', '.7xtgv-desktop-lock'))
    area = card / 'A/0'
    # Tests stay in bank 9; give only this disposable profile MBASIC's exit.
    ini = area / '7XTGVTRM.INI'
    ini.write_bytes(ini.read_bytes().replace(b'[bank9]\r\n', b'[bank9]\r\nexit_command=SYSTEM\\r\r\n', 1))
    (area / 'VTHELLO.COM').write_bytes(bytes.fromhex('1110010e09cd05001e580e04cd0500c9') + b'Browser Z80 guest works\r\n$')
    (area / 'VTLOOP.COM').write_bytes(bytes.fromhex('c30001'))
    for drive in 'ABCDEFGHIJKLMNOP':
        for user in '0123456789ABCDEF': (card / drive / user).mkdir(parents=True, exist_ok=True)
    before = hashes(card)
    session = root / 'host'
    log = (root / 'host.log').open('wb')
    server = await asyncio.create_subprocess_exec(sys.executable, str(HERE / 'browser.py'),
        '--card-root', str(card), '--session-dir', str(session), '--no-open', stdout=log, stderr=log)
    errors, console, requests = [], [], []
    try:
        for _ in range(300):
            if (session / 'session.json').exists(): break
            if server.returncode is not None: raise AssertionError((root / 'host.log').read_text())
            await asyncio.sleep(.1)
        url = json.loads((session / 'session.json').read_text())['url']
        async with async_playwright() as p:
            browser = await p.chromium.launch(executable_path='/usr/bin/chromium', headless=True, args=['--no-sandbox'])
            context = await browser.new_context(viewport={'width':1200, 'height':1000}, accept_downloads=True,
                                                permissions=['clipboard-read', 'clipboard-write'])
            page = await context.new_page()
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.on('console', lambda m: console.append(m.type + ': ' + m.text))
            page.on('request', lambda r: requests.append((r.method, r.url, r.resource_type)))
            page.on('websocket', lambda _: errors.append('Unexpected WebSocket'))
            async def snap():
                return await page.evaluate('window.xtgvWeb.snapshot().text')
            async def until(marker, timeout=45):
                try:
                    await page.wait_for_function('(marker) => window.xtgvWeb?.phase !== "loading" && window.xtgvWeb.snapshot().text.includes(marker)', arg=marker, timeout=timeout*1000)
                except Exception:
                    (root / 'failed-screen.txt').write_text(await snap())
                    await page.screenshot(path=str(root / 'failed.png'))
                    raise
            async def focus():
                await page.click('#focus')
            async def command(text, marker=None, timeout=45):
                await focus()
                await page.keyboard.insert_text(text)
                await page.keyboard.press('Enter')
                if marker: await until(marker, timeout)
                await asyncio.sleep(.2)
            await page.goto(url)
            await until('A0>')
            assert await page.evaluate('crossOriginIsolated')
            await focus()
            await page.keyboard.press('Alt+9')
            await until('[#9 GENERAL]')
            await asyncio.sleep(1.4)
            print('a. Browser-only boot, bank #9 PASS', flush=True)
            # Pure host mappings paired with real parser/guest checks below.
            await page.evaluate('''async () => {
                const {encodeKey, keyEvent} = await import('./input.js');
                const decode = bytes => bytes && new TextDecoder().decode(bytes);
                const key = (key, code, mods=0, extra={}) => encodeKey({key, code, ctrlKey:!!(mods&1),shiftKey:!!(mods&2),altKey:!!(mods&4), ...extra});
                const check = (actual, expected) => {if (decode(actual)!==expected) throw Error(JSON.stringify([decode(actual),expected]));};
                for (const code of ['Digit0','Numpad0']) check(key('0',code,7),'\\x1b[>63;0v');
                for (let n=1;n<=9;n++) for (const prefix of ['Digit','Numpad'])
                    check(key(String(n),prefix+n,3),'\\x1b[>63;'+[0,4,7,11,14,18,21,25,28,31][n]+'v');
                check(key('q','KeyQ',4),'\\x1b[>60v');
                check(key('PageDown','PageDown',3),'\\x1b[>66v');
                check(key(',','NumpadDecimal'),decode(keyEvent('KeyKpDecimal',0,',')));
                check(key('ě','Digit2'),decode(keyEvent('KeyNum2',0,'ě')));
                check(encodeKey({key:'ě',code:'Digit2'},''),null);
                check(key('c','KeyC',5,{altGraph:true}),null);
                check(key('Dead','Quote'),null);
                check(key('é','KeyE',0,{isComposing:true}),null);
                check(key('F11','F11'),null);
            }''')
            await command('VTHELLO', 'Browser Z80 guest works')
            await command('VTDRV RW', 'VTDRV verify: PASS', 60)
            (root / 'storage.txt').write_text(await snap())
            print('b. Keyboard mapping + original guest/verified storage PASS', flush=True)
            await focus()
            await page.keyboard.insert_text('VTCPX')
            await page.keyboard.press('Backspace')
            await page.keyboard.insert_text('U')
            await page.keyboard.press('Enter')
            await until('VTCPU quiet')
            # Czech row and keypad go through private key events into C++ defaults.
            await focus()
            await page.evaluate('''() => {
                const input=document.querySelector('#keyboard');
                input.dispatchEvent(new KeyboardEvent('keydown',{key:'ě',code:'Digit2',bubbles:true,cancelable:true}));
                input.dispatchEvent(new KeyboardEvent('keydown',{key:',',code:'NumpadDecimal',bubbles:true,cancelable:true}));
            }''')
            await until('A0>2.')
            await page.keyboard.press('Backspace'); await page.keyboard.press('Backspace')
            # Capture is frozen while actual guest screen changes.
            await page.click('#show-text')
            frozen = await page.locator('#screen-text').text_content()
            await command('DIR PUN.TXT', 'PUN')
            assert await page.locator('#screen-text').text_content() == frozen
            await page.click('#refresh-text')
            assert 'PUN' in await page.locator('#screen-text').text_content()
            await page.click('#copy-text')
            await page.wait_for_function('document.querySelector("#notice").textContent === "Text copied."')
            assert await page.evaluate('navigator.clipboard.readText()') == await page.locator('#screen-text').text_content()
            async with page.expect_download() as dl:
                await page.click('#save-text')
            await (await dl.value).save_as(root / 'screen.txt')
            assert (root / 'screen.txt').read_text() == await page.locator('#screen-text').text_content()
            await page.click('#hide-text')
            # Text in settings must never enter the guest.
            old = await snap()
            await page.click('summary')
            await page.fill('#top-row', '')
            await page.click('#apply-row')
            await page.wait_for_timeout(150)
            assert await snap() == old
            await page.click('summary')
            await focus()
            await page.keyboard.press('Control+BracketRight')
            assert await page.evaluate('document.activeElement.id') != 'keyboard'
            print('c. Editing, numeric fallback, isolated focus, frozen/copy/download text PASS', flush=True)
            await focus()
            await page.keyboard.press('Control+Alt+Shift+Digit0')
            await page.wait_for_timeout(250)
            black = await page.evaluate('Array.from(document.querySelector("canvas").getContext("2d").getImageData(0,0,480,320).data).filter((v,i)=>i%4!==3).every(v=>v===0)')
            assert black, 'Brightness OFF did not black out framebuffer'
            await page.keyboard.press('Control+Shift+Digit9')
            await page.wait_for_timeout(250)
            assert await page.evaluate('Array.from(document.querySelector("canvas").getContext("2d").getImageData(0,0,480,320).data).some((v,i)=>i%4!==3 && v>0)')
            for n in (1,2,3,4):
                await page.select_option('#scale',str(n))
                size = await page.locator('#display').evaluate('(c)=>[c.width,c.height,c.getBoundingClientRect().width,c.getBoundingClientRect().height,c.getContext("2d").imageSmoothingEnabled,getComputedStyle(c).imageRendering]')
                assert size == [480,320,480*n,320*n,False,'pixelated'], size
            await page.select_option('#scale','2')
            await command('MBASIC', 'OK')
            await focus(); await page.keyboard.press('Alt+q')
            await until('A0>')
            await command('7XF','/a/cpm>')
            await command('CD /C/XTERM256','/c/xterm256>')
            await command('CLS')
            await focus()
            for _ in range(14): await page.keyboard.press('Enter')
            await command('SUBMIT BROT_BS','VTSTOP:',60)
            colors = await page.evaluate('''() => {
                const d=document.querySelector('canvas').getContext('2d').getImageData(0,0,480,320).data;
                return new Set(Array.from({length:d.length/4},(_,i)=>(d[i*4]<<16)|(d[i*4+1]<<8)|d[i*4+2])).size;
            }''')
            assert colors > 16, colors
            await page.screenshot(path=str(root / 'graphics.png'))
            async with page.expect_download() as dl: await page.click('#save-image')
            await (await dl.value).save_as(root / 'screen.png')
            data=(root / 'screen.png').read_bytes()
            assert int.from_bytes(data[16:20],'big') == 480 and int.from_bytes(data[20:24],'big') == 320
            await page.click('#copy-image')
            await page.wait_for_function('document.querySelector("#notice").textContent === "Image copied."')
            assert 'image/png' in await page.evaluate('navigator.clipboard.read().then(items=>items.flatMap(i=>i.types))')
            print('d. Backlight, scale 1..4, MBASIC exit, real graphics, PNG/clipboard PASS', flush=True)
            await command('CD /A/CPM','/a/cpm>')
            await command('REBOOT')
            await page.wait_for_timeout(1500)
            await until('A0>')
            await command('DIR PUN.TXT','PUN')
            await page.click('#stop')
            await page.wait_for_function('window.xtgvWeb.phase === "stopped"',timeout=30000)
            async with page.expect_download() as dl: await page.click('#export')
            await (await dl.value).save_as(root / 'card.zip')
            with zipfile.ZipFile(root / 'card.zip') as z:
                assert z.read('cpmcard/A/0/PUN.TXT') == b'X'
                assert len(z.read('cpmcard/A/0/VTDRV.$$$')) == 32768
            await page.set_input_files('#import',str(root / 'card.zip'))
            await until('A0>')
            await command('DIR PUN.TXT','PUN')
            # Offline after preload: no server participates in input, snapshots or ZIP.
            await context.set_offline(True)
            await command('VTLOOP')
            await page.keyboard.press('Control+Alt+c')
            await page.wait_for_function('window.xtgvWeb.snapshot().text.trimEnd().endsWith("A0>")', timeout=15000)
            await command('VTLOOP')
            await page.click('#stop')
            await page.wait_for_function('window.xtgvWeb.phase === "stopped"',timeout=30000)
            async with page.expect_download() as dl: await page.click('#export')
            await (await dl.value).save_as(root / 'offline-card.zip')
            assert all(method == 'GET' and kind != 'websocket' for method,_,kind in requests), requests
            assert not errors, errors
            print('e. REBOOT, ZIP round trip, offline busy-loop stop/export, static GET-only PASS', flush=True)
            await page.screenshot(path=str(root / 'stopped.png'))
            await browser.close()
    finally:
        if server.returncode is None:
            server.terminate(); await server.wait()
        log.close()
        (root / 'browser.log').write_text('\n'.join(console + errors))
        (root / 'requests.json').write_text(json.dumps(requests,indent=2))
    assert hashes(card) == before
    (root / 'PASS').write_text('Browser input, snapshot, graphics, card lifecycle, offline operation; source card unchanged.\n')
    print('All standalone checks PASS; host source unchanged.',flush=True)


if __name__ == '__main__':
    asyncio.run(main())
