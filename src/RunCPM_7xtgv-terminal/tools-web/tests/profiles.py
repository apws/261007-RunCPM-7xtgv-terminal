#!/usr/bin/env python3
"""Switch every compiled display in one browser, retaining a disposable card."""
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

EXPECTED = {'ES3_35': (480,320,80,32), 'EC6_147': (320,172,80,24),
            'EP4_10': (1280,800,212,80), 'ES3_5': (800,480,132,48),
            'RP2_147': (320,172,80,24), 'RP2_2': (320,240,80,24)}

async def main():
    root = Path(tempfile.mkdtemp(prefix='profiles-', dir=BUILD / 'sessions'))
    print('Evidence:', root, flush=True)
    card = root / 'card'
    shutil.copytree(ROOT / 'cpmcard', card, ignore=shutil.ignore_patterns('private', '.7xtgv-desktop-lock'))
    area = card / 'A/0'
    # Distinct INI sections exercise identity and maximum logical geometry.
    ini = '[global]\r\nccp_bank=9\r\n'
    for id, (w,h,cols,rows) in EXPECTED.items():
        ini += f'[display:{id}]\r\nview_zoom=1\r\nzoom1_terminal={cols}x{rows}\r\n'
    (area / '7XTGVTRM.INI').write_bytes(ini.encode() + b'\x1a')
    (area / 'VTMARK.COM').write_bytes(bytes.fromhex('1110010e09cd05001e580e04cd0500c9') + b'PROFILE CARD RETAINED\r\n$')
    # Oversized geometry must be rejected even with a larger backing allocation.
    msg = b'\x1b[>73;1;212;80vLIMIT CHECK COMPLETE\r\n$'
    (area / 'VTLIMIT.COM').write_bytes(bytes.fromhex('1109010e09cd0500c9') + msg)
    (area / 'PUN.TXT').unlink(missing_ok=True)
    before = hashes(card)
    session = root / 'host'
    log = (root / 'host.log').open('wb')
    server = await asyncio.create_subprocess_exec(sys.executable, str(HERE / 'browser.py'),
        '--card-root', str(card), '--session-dir', str(session), '--window-scale','1', '--no-open', stdout=log, stderr=log)
    errors, records = [], []
    wasm_hash = hashlib.sha256((BUILD / 'shared/xtgv_web.wasm').read_bytes()).hexdigest()
    try:
        for _ in range(300):
            if (session / 'session.json').exists(): break
            if server.returncode is not None: raise AssertionError((root / 'host.log').read_text())
            await asyncio.sleep(.1)
        url = json.loads((session / 'session.json').read_text())['url']
        async with async_playwright() as p:
            browser = await p.chromium.launch(executable_path='/usr/bin/chromium',headless=True,args=['--no-sandbox'])
            page = await browser.new_page(viewport={'width':1600,'height':1000},accept_downloads=True)
            page.on('pageerror',lambda e:errors.append(str(e)))
            async def until(marker):
                await page.wait_for_function('(s) => window.xtgvWeb?.phase === "running" && window.xtgvWeb.snapshot()?.text.includes(s)',arg=marker,timeout=45000)
            async def command(text,marker):
                await page.click('#focus'); await page.keyboard.insert_text(text); await page.keyboard.press('Enter'); await until(marker)
            await page.goto(url)
            await until('A0>')
            profiles = await page.evaluate('xtgvWeb.profiles')
            assert {p['id'] for p in profiles} == set(EXPECTED)
            await command('VTMARK','PROFILE CARD RETAINED')
            for profile in profiles:
                target, id = profile['target'], profile['id']
                if await page.evaluate('xtgvWeb.target') != target:
                    await page.select_option('#target',target)
                    await page.click('#switch-target')
                    await page.wait_for_function('(t) => xtgvWeb.phase === "running" && xtgvWeb.target === t',arg=target,timeout=45000)
                await until('A0>')
                await command('DIR PUN.TXT','PUN')
                w,h,cols,rows = EXPECTED[id]
                actual = await page.evaluate('({width:display.width,height:display.height,...xtgvWeb.snapshot()})')
                assert (actual['width'],actual['height'],actual['cols'],actual['rows']) == (w,h,cols,rows),(id,actual)
                assert actual['viewCols'] == min(cols,(w//6)&~1) and actual['viewRows'] == min(rows,h//10),(id,actual)
                await command('VTLIMIT','LIMIT CHECK COMPLETE')
                assert (await page.evaluate('xtgvWeb.snapshot().cols')) == cols,id
                await page.click('#focus'); await page.keyboard.press('Control+Shift+PageDown')
                await page.wait_for_function('(n) => xtgvWeb.snapshot().viewCols === n',arg=min(cols,(w//12)&~1))
                await page.keyboard.press('Control+Shift+PageUp')
                await page.wait_for_function('(n) => xtgvWeb.snapshot().viewCols === n',arg=min(cols,(w//6)&~1))
                await page.keyboard.press('Control+Alt+Shift+0')
                await page.wait_for_function('!Array.from(display.getContext("2d").getImageData(0,0,display.width,display.height).data).some((n,i)=>i%4!==3 && n)',timeout=15000)
                await page.keyboard.press('Control+Shift+9')
                await page.wait_for_function('Array.from(display.getContext("2d").getImageData(0,0,display.width,display.height).data).some((n,i)=>i%4!==3 && n)',timeout=15000)
                await page.screenshot(path=str(root / (id+'.png')))
                records.append({'id':id,'snapshot':actual,'duties':profile['duties']})
                print('a. '+id+' geometry, limits, zoom, brightness, retained card PASS',flush=True)
            await command('REBOOT','A0>')
            await page.wait_for_timeout(1500)
            await until('A0>')
            assert await page.evaluate('xtgvWeb.target') == profiles[-1]['target']
            await command('DIR PUN.TXT','PUN')
            await page.click('#stop'); await page.wait_for_function('xtgvWeb.phase === "stopped"',timeout=30000)
            async with page.expect_download() as d: await page.click('#export')
            await (await d.value).save_as(root/'retained.zip')
            with zipfile.ZipFile(root/'retained.zip') as z: assert z.read('cpmcard/A/0/PUN.TXT') == b'X'
            # Selection from stopped state also uses the retained card.
            await page.select_option('#target',profiles[0]['target']); await page.click('#switch-target'); await until('A0>')
            await command('DIR PUN.TXT','PUN')
            await page.click('#stop'); await page.wait_for_function('xtgvWeb.phase === "stopped"',timeout=30000)
            print('b. REBOOT, stopped switch and byte-exact ZIP retention PASS',flush=True)
            from urllib.parse import urlsplit,parse_qs,urlencode,urlunsplit
            u=urlsplit(url); q=parse_qs(u.query); q['target']=['INVALID']
            await page.goto(urlunsplit((u.scheme,u.netloc,u.path,urlencode(q,doseq=True),'')))
            await page.wait_for_function('xtgvWeb.phase === "failed"',timeout=30000)
            assert 'Unknown' in await page.locator('#status').text_content()
            await browser.close()
        assert not errors,errors
        assert hashes(card)==before,'Source fixture changed'
        assert hashlib.sha256((BUILD/'shared/xtgv_web.wasm').read_bytes()).hexdigest()==wasm_hash
        (root/'results.json').write_text(json.dumps({'wasm_sha256':wasm_hash,'profiles':records},indent=2))
        (root/'PASS').write_text('Six profiles, shared Wasm, retained card, geometry limits, zoom, brightness, unknown target rejection\n')
        print('c. One Wasm hash; source fixture unchanged; unknown target rejected PASS',flush=True)
    finally:
        if server.returncode is None: server.terminate()
        await server.wait(); log.close()

if __name__ == '__main__': asyncio.run(main())
