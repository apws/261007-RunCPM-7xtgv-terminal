#!/usr/bin/env python3
"""Exercise the actual Pages package without server-side isolation headers."""
import asyncio
import functools
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import zipfile

from playwright.async_api import async_playwright
from proof import HERE, BUILD, ROOT
sys.path.insert(0, str(HERE))
from pages import OUTPUT, PreviewHandler


async def main():
    (BUILD / 'sessions').mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='pages-proof-', dir=BUILD / 'sessions'))
    site = root / 'repository'
    shutil.copytree(OUTPUT, site)
    manifest = json.loads((site / 'package.json').read_text())
    with zipfile.ZipFile(site / 'card.zip') as card:
        for name in ('7XTGVTRM.INI', '7XTGVTRM.BUF'):
            committed = subprocess.check_output(['git', 'show', f'{manifest["card_commit"]}:cpmcard/A/0/{name}'], cwd=ROOT)
            assert card.read('cpmcard/A/0/' + name) == committed
        assert all(f'cpmcard/{d}/{u}/' in card.namelist() for d in 'ABCDEFGHIJKLMNOP' for u in '0123456789ABCDEF')
        assert not any('private' in p.split('/') for p in card.namelist())
    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(PreviewHandler, directory=str(root)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = f'http://127.0.0.1:{server.server_port}/repository/?scale=2&target=ES3_35'
    print('Evidence:', root, flush=True)
    try:
        async with async_playwright() as p:
            browser = await p.chromium.launch(executable_path=os.environ.get('XTGV_TEST_BROWSER', '/usr/bin/chromium') or None,
                                              headless=True, args=['--no-sandbox'])
            context = await browser.new_context(accept_downloads=True)
            await context.add_init_script("sessionStorage.loads = String(Number(sessionStorage.loads || 0) + 1)")
            response = await context.request.get(url)
            assert 'cross-origin-opener-policy' not in response.headers
            page = await context.new_page()
            errors, navigations = [], []
            page.on('pageerror', lambda error: errors.append(str(error)))
            page.on('framenavigated', lambda frame: navigations.append(frame.url) if frame == page.main_frame else None)
            await page.goto(url)
            await page.wait_for_function('window.xtgvWeb?.phase === "running" && xtgvWeb.snapshot().text.includes("A0>")', timeout=60000)
            assert await page.evaluate('Number(sessionStorage.loads)') == 2, navigations
            assert page.url == url.replace('ES3_35', 'jc8048w535')
            assert await page.evaluate('crossOriginIsolated && typeof SharedArrayBuffer === "function"')
            assert await page.evaluate('navigator.serviceWorker.controller.scriptURL.endsWith("/repository/pages-worker.js")')
            await page.keyboard.press('Alt+9')
            await page.wait_for_timeout(1200)
            await page.keyboard.insert_text('DIR')
            await page.keyboard.press('Enter')
            await page.wait_for_function('xtgvWeb.snapshot().text.includes("VTCPU")')
            await page.select_option('#target', 'rp2350-touch-lcd-2')
            await page.click('#switch-target')
            await page.wait_for_function('xtgvWeb.phase === "running" && xtgvWeb.target === "rp2350-touch-lcd-2" && xtgvWeb.snapshot().text.includes("A0>")')
            await page.click('#stop')
            await page.wait_for_function('xtgvWeb.phase === "stopped"')
            # Returning visit is already controlled and boots without another reload.
            navigations.clear()
            await page.reload()
            await page.wait_for_function('window.xtgvWeb?.phase === "running" && xtgvWeb.snapshot().text.includes("A0>")')
            assert await page.evaluate('Number(sessionStorage.loads)') == 3, navigations
            # A new worker waits; it cannot interrupt an active volatile card.
            with (site / 'pages-worker.js').open('a') as file:
                file.write('\n// Test update: must wait until active tabs close.\n')
            await page.evaluate('navigator.serviceWorker.getRegistration().then(r => r.update())')
            await page.wait_for_function('navigator.serviceWorker.getRegistration().then(r => !!r.waiting)')
            assert await page.evaluate('Number(sessionStorage.loads)') == 3
            assert await page.evaluate('xtgvWeb.phase') == 'running'
            await page.click('#stop')
            await page.wait_for_function('xtgvWeb.phase === "stopped"')
            await page.screenshot(path=str(root / 'pages.png'), full_page=True)
            assert not errors, errors
            await context.close()
            # Unsupported isolation fails visibly, without loading the emulator.
            context = await browser.new_context(service_workers='block')
            page = await context.new_page()
            await page.goto(url)
            await page.wait_for_function('document.querySelector("#status").textContent === "Browser startup unavailable"', timeout=25000)
            assert 'timed out' in await page.locator('#notice').inner_text()
            assert await page.evaluate('typeof window.xtgvWeb') == 'undefined'
            await context.close()
            await browser.close()
        (root / 'PASS').write_text('Committed card + DU dirs; headerless subpath first/return boot; query retained; shared-memory worker; DIR; target switch; update waits without reload; blocked workers fail before guest startup.\n')
        print('Pages hosting checks PASS', flush=True)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == '__main__':
    asyncio.run(main())
