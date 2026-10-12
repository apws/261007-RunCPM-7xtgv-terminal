import {KeyboardModule} from './input.js';
import {SerialLink} from './serial_link.js';
import {Display, download} from './display.js';
import {ScreenText} from './screen_text.js';
import {unpackCard, installCard, snapshotCard, packCard, CARD_LIMIT} from './card.js';
import {defaultTopRow} from './protocol.js';
const $ = id => document.getElementById(id);
const notice = text => {$('notice').textContent = text;};
const link = new SerialLink();
let runtime = null, phase = 'loading', epoch = 0, lastSnapshot = null, completed = null;
const scale = Math.max(1, Math.min(9, Number(new URLSearchParams(location.search).get('scale')) || 2));
const display = new Display($('display'), $('geometry'), scale);
for (let n=1;n<=9;n++) $('scale').add(new Option(n + '×', n, n === scale, n === scale));
$('scale').onchange = () => display.setScale($('scale').value);
const keyboard = new KeyboardModule($('display-area'), $('keyboard'), bytes => link.send(bytes), notice, defaultTopRow);
$('keyboard').addEventListener('blur', () => link.cancelPending());
$('focus').onclick = () => $('keyboard').focus({preventScroll:true});
$('top-row').value = defaultTopRow;
$('apply-row').onclick = () => {
    const row = $('top-row').value;
    if (row && (Array.from(row).length !== 10 || /[\x00-\x1f]/.test(row))) {notice('Use ten characters, or empty to disable.'); return;}
    keyboard.topRow = row; notice('Keyboard setting applied for this session.');
};
function getSnapshot() {
    if (!runtime || phase === 'loading') throw Error('Screen is not ready.');
    runtime._web_snapshot();
    return lastSnapshot;
}
const textView = new ScreenText($('text-panel'), $('screen-text'), $('text-info'), getSnapshot);
function action(id, fn) {$(id).onclick = async () => {try {await fn();} catch (error) {notice(error.message);}};}
action('show-text', () => textView.refresh());
action('refresh-text', () => textView.refresh());
action('copy-text', async () => {await textView.copy(); notice('Text copied.');});
action('save-text', () => textView.download());
action('hide-text', () => {$('text-panel').hidden = true;});
action('save-image', async () => download(await display.png(), '7xtgv-screen.png'));
action('copy-image', async () => {
    if (!navigator.clipboard?.write || !window.ClipboardItem) throw Error('Image clipboard unavailable; use Download PNG.');
    // Supply a promise within the click gesture (also accepted by stricter browsers).
    await navigator.clipboard.write([new ClipboardItem({'image/png': display.png()})]);
    notice('Image copied.');
});
function setPhase(value, message) {
    phase = value; $('status').textContent = message;
    keyboard.setEnabled(value === 'running');
    $('focus').disabled = value !== 'running';
    $('stop').disabled = value !== 'running';
    $('export').disabled = value !== 'stopped';
    $('restart').disabled = value !== 'stopped';
    $('import').disabled = !['stopped', 'failed'].includes(value);
    for (const id of ['show-text','refresh-text','copy-image','save-image']) $(id).disabled = !runtime || value === 'loading';
}
function fail(error) {
    console.error(error);
    link.clear();
    if (runtime && phase === 'running') runtime._web_stop();
    setPhase('failed', 'Failed: ' + error.message);
}
async function start(card) {
    setPhase('loading', 'Starting RunCPM…'); link.clear();
    const generation = ++epoch;
    completed = null;
    if (runtime) {runtime.PThread.terminateAllThreads(); runtime = null;}
    const instance = await createXtgv({noInitialRun:true, print:console.log, printErr:console.error,
        onAbort: error => fail(Error(String(error))),
        mirrorReady: () => true,
        mirrorOutput: () => {}, // Always drain TX. Snapshots do not need ANSI replay.
        screenSnapshot: snapshot => {lastSnapshot = snapshot;},
        presentFrame: (...args) => display.present(...args),
        guestFinished: (code, error) => {
            if (generation !== epoch) return;
            link.clear();
            // Defer replacing the instance until this C++ callback has returned.
            queueMicrotask(() => {
                if (code === 75) {
                    try {start(snapshotCard(instance.FS)).catch(fail);} catch (error) {fail(error);}
                } else if (code === 0) {
                    try {
                        completed = snapshotCard(instance.FS);
                        instance.PThread.terminateAllThreads();
                        setPhase('stopped', 'Stopped cleanly. Download the card ZIP to keep changes.');
                    } catch (error) {fail(error);}
                } else fail(Error(error || 'Guest stopped with an error.'));
            });
        }
    });
    runtime = instance;
    installCard(runtime.FS, card);
    runtime._web_connect(1);
    runtime.callMain([]);
    link.attach(runtime);
    setPhase('running', 'Running · card changes are in memory. Click the display to type.');
}
action('stop', () => {setPhase('stopping', 'Stopping and saving…');link.clear();runtime._web_stop();});
action('restart', () => start(completed));
action('export', () => {if (phase === 'stopped') download(packCard(completed), 'cpmcard-web.zip');});
$('import').onchange = async () => {
    const file = $('import').files[0]; $('import').value = '';
    if (!file || !['stopped', 'failed'].includes(phase)) return;
    try {
        if (file.size > CARD_LIMIT) throw Error('Card ZIP exceeds 32 MiB.');
        const card = unpackCard(new Uint8Array(await file.arrayBuffer()));
        await start(card);
    } catch (error) {notice(error.message);}
};
setInterval(() => {try {link.pump();} catch (error) {fail(error);}}, 5);
window.addEventListener('beforeunload', e => {
    if (['running', 'stopping'].includes(phase)) {e.preventDefault(); e.returnValue = '';}
});
// Read-only diagnostics used by the browser integration tests, also useful in DevTools.
window.xtgvWeb = {get phase() {return phase;}, snapshot: getSnapshot};
(async () => {
    if (!crossOriginIsolated) throw Error('Hosting needs COOP/COEP headers for the RunCPM worker.');
    const response = await fetch('card.zip');
    if (!response.ok) throw Error('Preload card is missing. Load a card ZIP to continue.');
    await start(unpackCard(new Uint8Array(await response.arrayBuffer())));
})().catch(fail);
