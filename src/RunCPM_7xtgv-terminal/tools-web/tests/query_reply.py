#!/usr/bin/env python3
"""Guest query replies must beat VTINFO/VTZOOM polling timeouts on every web board."""
import asyncio
import json
from pathlib import Path
import shutil
import sys
import tempfile

from playwright.async_api import async_playwright
from proof import BUILD, HERE, ROOT, hashes


async def main():
    (BUILD / 'sessions').mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='query-reply-', dir=BUILD / 'sessions'))
    print('Evidence:', root, flush=True)
    card = root / 'card'
    shutil.copytree(ROOT / 'cpmcard', card,
                    ignore=shutil.ignore_patterns('private', '.7xtgv-desktop-lock'))
    # All automation stays in bank 9 and a disposable source card.
    (card / 'A/0/7XTGVTRM.INI').write_bytes(b'[global]\r\nccp_bank=9\r\n\x1a')
    before = hashes(card)
    session = root / 'host'
    log = (root / 'server.log').open('wb')
    server = await asyncio.create_subprocess_exec(
        sys.executable, str(HERE / 'browser.py'), '--card-root', str(card),
        '--session-dir', str(session), '--window-scale', '1', '--no-open',
        stdout=log, stderr=log)
    try:
        for _ in range(300):
            if (session / 'session.json').exists():
                break
            if server.returncode is not None:
                raise AssertionError((root / 'server.log').read_text())
            await asyncio.sleep(.1)
        url = json.loads((session / 'session.json').read_text())['url']
        async with async_playwright() as playwright:
            browser = await playwright.chromium.launch(
                executable_path='/usr/bin/chromium', headless=True, args=['--no-sandbox'])
            page = await browser.new_page(viewport={'width':1800, 'height':1000})
            errors = []
            page.on('pageerror', lambda error: errors.append(str(error)))
            await page.goto(url)
            await page.wait_for_function(
                'window.xtgvWeb?.phase === "running" && xtgvWeb.snapshot().text.includes("A0>")',
                timeout=45000)

            async def command(text):
                await page.click('#focus')
                await page.keyboard.insert_text(text)
                await page.keyboard.press('Enter')

            async def query(text, marker, identity=None):
                await command('CLS')
                await page.wait_for_function('xtgvWeb.snapshot().text.trim() === "A0>"')
                await command(text)
                try:
                    await page.wait_for_function('''() => {
                        const s = xtgvWeb.snapshot().text.trim();
                        return s.includes('A0>') && s.endsWith('A0>') && s.length > 8;
                    }''', timeout=15000)
                except Exception:
                    (root / 'failed.txt').write_text(await page.evaluate('xtgvWeb.snapshot().text'))
                    await page.screenshot(path=str(root / 'failed.png'))
                    raise
                # A late reply must not spill into an otherwise valid CCP prompt.
                await page.wait_for_timeout(200)
                snapshot = await page.evaluate('xtgvWeb.snapshot().text')
                assert marker in snapshot, snapshot
                assert all(bad not in snapshot for bad in
                           ('NOT SUPPORTED', 'BAD REPLY', 'INPUT PENDING', '7xtgv-info;')), snapshot
                assert snapshot.rstrip().endswith('A0>'), snapshot
                if identity:
                    assert f'Display : {identity}' in snapshot, snapshot
                return snapshot

            profiles = await page.evaluate('xtgvWeb.profiles')
            for index, profile in enumerate(profiles):
                if await page.evaluate('xtgvWeb.target') != profile['target']:
                    await page.select_option('#target', profile['target'])
                    await page.click('#switch-target')
                    await page.wait_for_function('''target => xtgvWeb.phase === 'running' &&
                        xtgvWeb.target === target && xtgvWeb.snapshot().text.includes('A0>')''',
                        arg=profile['target'], timeout=45000)
                for repeat in range(5):
                    snapshot = await query('VTINFO', 'Resolved presets:', profile['id'])
                    (root / f'{profile["id"]}-{repeat}.txt').write_text(snapshot)
                await page.screenshot(path=str(root / f'{profile["id"]}.png'))
                await query('VTZOOM', 'View    : zoom')
                await query('VTZOOM 2', 'View    : zoom 2')
                assert (await page.evaluate('xtgvWeb.snapshot().viewCols')) <= profile['width'] // 12
                await query('VTZOOM 1', 'View    : zoom 1')
                print(f'{chr(97 + index)}. {profile["id"]}: five VTINFO reports, VTZOOM query/2/1 and clean prompt PASS', flush=True)
            await page.click('#stop')
            await page.wait_for_function('xtgvWeb.phase === "stopped"', timeout=30000)
            assert not errors, errors
            await browser.close()
        assert hashes(card) == before, 'Source card changed'
    finally:
        if server.returncode is None:
            server.terminate()
        await server.wait()
        log.close()


if __name__ == '__main__':
    asyncio.run(main())
