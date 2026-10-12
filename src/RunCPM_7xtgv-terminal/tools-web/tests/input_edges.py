#!/usr/bin/env python3
"""Focused DOM input/composition and display-density checks (no guest card writes)."""
import asyncio
import json
from pathlib import Path
import sys
import tempfile
from playwright.async_api import async_playwright
from proof import HERE, BUILD


async def main():
    root = Path(tempfile.mkdtemp(prefix='browser-edges-', dir=BUILD / 'sessions'))
    print('Evidence:',root,flush=True)
    with (root / 'server.log').open('wb') as log:
        server=await asyncio.create_subprocess_exec(sys.executable,str(HERE/'browser.py'),
            '--session-dir',str(root/'host'),'--no-open',stdout=log,stderr=log)
        try:
            for _ in range(100):
                if (root/'host/session.json').exists(): break
                await asyncio.sleep(.1)
            url=json.loads((root/'host/session.json').read_text())['url']
            async with async_playwright() as p:
                browser=await p.chromium.launch(executable_path='/usr/bin/chromium',headless=True,args=['--no-sandbox'])
                for ratio in (1,1.25,2):
                    context=await browser.new_context(device_scale_factor=ratio,viewport={'width':1100,'height':1000})
                    page=await context.new_page()
                    await page.goto(url)
                    await page.wait_for_function('window.xtgvWeb?.phase === "running" && window.xtgvWeb.snapshot().text.includes("A0>")')
                    await page.wait_for_timeout(1200)
                    await page.evaluate('''async () => {
                        const {KeyboardModule}=await import('./input.js');
                        const area=document.createElement('div'), input=document.createElement('textarea');
                        area.append(input); document.body.append(area);
                        const sent=[], messages=[];
                        const module=new KeyboardModule(area,input,b=>sent.push(Array.from(b)),m=>messages.push(m),'');
                        module.setEnabled(true); input.focus();
                        const check=(yes,why)=>{if(!yes)throw Error(why);};
                        input.dispatchEvent(new CompositionEvent('compositionstart'));
                        input.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',code:'Enter',isComposing:false,cancelable:true}));
                        input.value='E';
                        input.dispatchEvent(new InputEvent('input',{data:'E',isComposing:true}));
                        check(sent.length===0,'Provisional composition was sent');
                        input.dispatchEvent(new CompositionEvent('compositionend',{data:'E'}));
                        input.dispatchEvent(new InputEvent('input',{data:'E',isComposing:false}));
                        await Promise.resolve();
                        check(JSON.stringify(sent)==='[[69]]','Composition duplicated or lost');
                        input.value='é'; input.dispatchEvent(new InputEvent('input',{data:'é'}));
                        check(sent.length===1 && messages.at(-1).includes('ASCII'),'Unsupported text was not reported');
                        const paste=new DataTransfer();paste.setData('text/plain','ONE\\r\\nTWO\\n');
                        input.dispatchEvent(new ClipboardEvent('paste',{clipboardData:paste,cancelable:true}));
                        check(new TextDecoder().decode(new Uint8Array(sent.at(-1)))==='ONE\\rTWO\\r','Paste CRLF conversion');
                        const count=sent.length;module.text('BAD\\x1b[>60v');
                        check(sent.length===count,'Pasted CSI entered terminal');
                        input.dispatchEvent(new KeyboardEvent('keydown',{key:'AltGraph',code:'AltRight',ctrlKey:true,altKey:true}));
                        input.dispatchEvent(new KeyboardEvent('keydown',{key:'c',code:'KeyC',ctrlKey:true,altKey:true,cancelable:true}));
                        check(sent.length===count,'Right Alt interpreted as emergency chord');
                        input.blur(); input.focus();
                        input.dispatchEvent(new KeyboardEvent('keydown',{key:'c',code:'KeyC',ctrlKey:true,altKey:true,cancelable:true}));
                        check(sent.length===count+1,'Blur did not clear Right Alt tracking');
                        area.remove();
                    }''')
                    await page.select_option('#scale','3')
                    dimensions=await page.locator('#display').evaluate('(c)=>[devicePixelRatio,c.width,c.height,c.getBoundingClientRect().width,c.getBoundingClientRect().height,getComputedStyle(c).imageRendering]')
                    assert dimensions==[ratio,480,320,1440,960,'pixelated'],dimensions
                    await page.click('#focus')
                    old=await page.evaluate('window.xtgvWeb.snapshot()')
                    await page.keyboard.press('Control+Shift+PageDown')
                    await page.wait_for_function('(cols)=>window.xtgvWeb.snapshot().viewCols < cols', arg=old['viewCols'])
                    zoomed=await page.evaluate('window.xtgvWeb.snapshot()')
                    assert zoomed['viewCols'] < old['viewCols'],(old['viewCols'],zoomed['viewCols'])
                    await page.keyboard.press('Control+Shift+PageUp')
                    await page.wait_for_timeout(250)
                    assert (await page.evaluate('window.xtgvWeb.snapshot().viewCols')) == old['viewCols']
                    await page.set_viewport_size({'width':700,'height':800})
                    assert await page.locator('#display').evaluate('(c)=>c.getBoundingClientRect().width')==1440
                    await page.click('#stop')
                    await page.wait_for_function('window.xtgvWeb.phase === "stopped"')
                    await page.screenshot(path=str(root/f'dpr-{ratio}.png'))
                    await context.close()
                    print(f'DPR {ratio}: composition/AltGr/paste/focus, integer CSS scale, glyph zoom PASS',flush=True)
                await browser.close()
            (root/'PASS').write_text('DOM composition once; AltGr/focus/paste guards; integer CSS size at DPR 1, 1.25, 2; real glyph zoom.\n')
        finally:
            if server.returncode is None:server.terminate();await server.wait()


if __name__=='__main__':asyncio.run(main())
