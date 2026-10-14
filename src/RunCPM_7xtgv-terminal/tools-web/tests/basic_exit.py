#!/usr/bin/env python3
"""Real browser Alt+Q must stop BASIC tools cleanly before exiting MBASIC."""
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
    root = Path(tempfile.mkdtemp(prefix='basic-exit-', dir=BUILD / 'sessions'))
    print('Evidence:', root, flush=True)
    card = root / 'card'
    (card / 'A/0').mkdir(parents=True)
    for name in ('MBASIC.COM', 'VTPAL.BAS', 'VTESTS.BAS'):
        shutil.copyfile(ROOT / 'cpmcard/C/1' / name, card / 'A/0' / name)
    # All automation uses bank 9 in a disposable card.
    (card / 'A/0/7XTGVTRM.INI').write_bytes(
        b'[global]\r\nccp_bank=9\r\n[bank9]\r\n'
        b'input_adapter=local_compose\r\nscreen_line=true\r\n'
        b'command_new_line=true\r\nexit_command=system\\r\r\n'
        b'clear_command=print chr$(27);"[2J";chr$(27);"[H"\\r\r\n\x1a')
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

            for program in ('VTPAL', 'VTESTS'):
                await command('MBASIC ' + program)
                marker = 'Choose preferred preset last.' if program == 'VTPAL' else 'Select:'
                await page.wait_for_function('s => xtgvWeb.snapshot().text.includes(s)', arg=marker)
                if program == 'VTPAL':
                    await page.keyboard.press('s')
                    await page.wait_for_timeout(100)
                    assert marker in await page.evaluate('xtgvWeb.snapshot().text')
                else:
                    await command('5')
                    await page.wait_for_function(
                        'xtgvWeb.snapshot().text.includes("Enter: return to ANSI")')
                await page.keyboard.press('Alt+q')
                try:
                    await page.wait_for_function('xtgvWeb.snapshot().text.trim().endsWith("OK")')
                    await page.wait_for_timeout(400)
                    snapshot = await page.evaluate('xtgvWeb.snapshot().text')
                    assert snapshot.strip().endswith('OK'), snapshot
                    assert 'error' not in snapshot.lower() and 'Break' not in snapshot, snapshot
                    (root / (program + '-ok.txt')).write_text(snapshot)
                    await command('PRINT 12345')
                    await page.wait_for_function('''() => {
                        const s = xtgvWeb.snapshot().text;
                        return s.includes(' 12345') && s.trim().endsWith('OK');
                    }''')
                    # A long MBASIC clear expression must arrive as one burst,
                    # not spend ~1.7 s being typed at the default exit delay.
                    began = asyncio.get_running_loop().time()
                    await page.keyboard.press('Alt+Backspace')
                    await page.wait_for_function('xtgvWeb.snapshot().text.trim() === "OK"')
                    elapsed = asyncio.get_running_loop().time() - began
                    assert elapsed < .75, f'Clear took {elapsed:.3f} s'
                    print(f'{program}: clear at OK in {elapsed * 1000:.0f} ms', flush=True)
                    await page.keyboard.press('Alt+q')
                    await page.wait_for_function('xtgvWeb.snapshot().text.trim().endsWith("A0>")')
                except Exception:
                    (root / 'failed.txt').write_text(await page.evaluate('xtgvWeb.snapshot().text'))
                    await page.screenshot(path=str(root / 'failed.png'))
                    raise
                print(program + ': Alt+Q -> clean OK; next Alt+Q -> CP/M PASS', flush=True)
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
