import {KeyboardModule} from './input.js';
import {SerialLink} from './serial_link.js';
import {Display, download} from './display.js';
import {ScreenText} from './screen_text.js';
import {unpackCard, installCard, snapshotCard, packCard, CARD_LIMIT} from './card.js';
import {defaultTopRow} from './protocol.js';
const $ = id => document.getElementById(id);
for (const key of document.querySelectorAll('kbd')) {
    key.classList.toggle('modifier', /^(ctrl|alt|altgr|shift|fn)$/i.test(key.textContent.trim()));
}
$('toggle-guides').onclick = () => {
    const hidden = !$('guides-column').hidden;
    $('guides-column').hidden = hidden;
    $('workspace').classList.toggle('guides-hidden', hidden);
    $('toggle-guides').setAttribute('aria-expanded', String(!hidden));
    $('toggle-guides').textContent = hidden ? 'Show guides' : 'Hide guides';
};
const notice = text => {$('notice').textContent = text;};
const link = new SerialLink();
let selectedTarget = new URLSearchParams(location.search).get('target') || 'jc8048w535';
let pendingTarget = null, profiles = [];
let runtime = null, phase = 'loading', epoch = 0, lastSnapshot = null, completed = null;
const scale = Math.max(1, Math.min(9, Number(new URLSearchParams(location.search).get('scale')) || 2));
const display = new Display($('display'), $('geometry'), scale);
for (let n=1;n<=9;n++) $('scale').add(new Option(n + '×', n, n === scale, n === scale));
$('scale').onchange = () => display.setScale($('scale').value);
const touchHeightSelect = $('touch-height');
let touchHeight = 5;
try {
    const saved = Number(localStorage.getItem('xtgv-touch-height'));
    if (Number.isInteger(saved) && saved >= 1 && saved <= 10) touchHeight = saved;
} catch {}
for (let n=1;n<=10;n++) touchHeightSelect.add(new Option(`${100 + (n - 1) * 10}%`, n, n === touchHeight, n === touchHeight));
function applyTouchHeight(value) {
    const factor = 1 + (value - 1) / 10;
    const style = document.documentElement.style;
    style.setProperty('--touch-key-size', `${20 * factor}px`);
    style.setProperty('--touch-sixpack-height', `${45 * factor}px`);
    style.setProperty('--touch-key-gap', `${5 * factor}px`);
    style.setProperty('--touch-key-pad', `${2 * factor}px`);
    style.setProperty('--touch-key-font-size', `${Math.min(12 * factor, 14)}px`);
    style.setProperty('--touch-arrow-font-size', `${11.2 * factor}px`);
}
applyTouchHeight(touchHeight);
touchHeightSelect.onchange = () => {
    touchHeight = Number(touchHeightSelect.value);
    applyTouchHeight(touchHeight);
    try {localStorage.setItem('xtgv-touch-height', String(touchHeight));} catch {}
};
const keyboard = new KeyboardModule($('display-area'), $('keyboard'), bytes => link.send(bytes), notice, defaultTopRow);
$('keyboard').addEventListener('blur', () => link.cancelPending());
$('touch-keypad').addEventListener('pointerdown', event => event.preventDefault());
let touchModifiers = 0;
const touchModifierBits = {ctrl:1, shift:2, alt:4};
const touchModifierButtons = [...document.querySelectorAll('[data-touch-mod]')];
function updateTouchModifiers() {
    touchModifiers = keyboard.stickyModifiers;
    for (const button of touchModifierButtons) {
        const active = !!(touchModifiers & touchModifierBits[button.dataset.touchMod]);
        button.setAttribute('aria-pressed', String(active));
    }
}
keyboard.onStickyModifiersChange = updateTouchModifiers;
for (const button of touchModifierButtons) button.onclick = () => {
    keyboard.setStickyModifiers(touchModifiers ^ touchModifierBits[button.dataset.touchMod]);
};
for (const button of document.querySelectorAll('[data-touch-key]')) button.onclick = () => {
    const key = button.dataset.touchKey;
    if (keyboard.touchKey(key, key, touchModifiers)) {
        keyboard.setStickyModifiers(0);
    }
};
$('focus').onclick = () => keyboard.focus();
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
    if (value !== 'running') keyboard.setStickyModifiers(0);
    $('focus').disabled = value !== 'running';
    $('stop').disabled = value !== 'running';
    $('export').disabled = value !== 'stopped';
    $('restart').disabled = value !== 'stopped';
    $('import').disabled = !['stopped', 'failed'].includes(value);
    $('target').disabled = !['running', 'stopped'].includes(value);
    $('switch-target').disabled = !['running', 'stopped'].includes(value);
    for (const id of ['show-text','refresh-text','copy-image','save-image']) $(id).disabled = !runtime || value === 'loading';
}
function fail(error) {
    console.error(error);
    pendingTarget = null;
    link.clear();
    if (runtime && phase === 'running') runtime._web_stop();
    setPhase('failed', 'Failed: ' + error.message);
}
async function start(card, target = selectedTarget) {
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
                if (code === 0 || code === 75) {
                    try {
                        completed = snapshotCard(instance.FS);
                        instance.PThread.terminateAllThreads();
                        if (pendingTarget || code === 75) {
                            const next = pendingTarget || selectedTarget; pendingTarget = null;
                            start(completed, next).catch(fail);
                            return;
                        }
                        setPhase('stopped', 'Stopped cleanly. Download the card ZIP to keep changes.');
                    } catch (error) {fail(error);}
                } else fail(Error(error || 'Guest stopped with an error.'));
            });
        }
    });
    runtime = instance;
    runtime._web_profiles();
    profiles = runtime.boardProfiles;
    const profile = profiles.find(p => p.target === target || p.id === target);
    if (!profile || !runtime.ccall('web_select_profile', 'number', ['string'], [profile.target]))
        throw Error('Unknown or unavailable target: ' + target);
    selectedTarget = profile.target;
    $('target').replaceChildren(...profiles.map(p => new Option(`${p.id} · ${p.width}×${p.height}`, p.target, p.target === selectedTarget, p.target === selectedTarget)));
    $('target-title').textContent = profile.id + ' · browser';
    document.title = 'RunCPM 7xtgv-terminal · ' + profile.id;
    $('target').title = profile.name;
    display.setProfile(profile);
    $('text-panel').hidden = true; lastSnapshot = null;
    const url = new URL(location.href); url.searchParams.set('target', selectedTarget);
    history.replaceState(null, '', url);
    installCard(runtime.FS, card);
    runtime._web_connect(1);
    runtime.callMain([]);
    link.attach(runtime);
    setPhase('running', 'Running · card changes are in memory. Click the display to type.');
    keyboard.focus();
}
action('stop', () => {setPhase('stopping', 'Stopping and saving…');link.clear();runtime._web_stop();});
action('restart', () => start(completed));
action('switch-target', () => {
    const target = $('target').value;
    if (target === selectedTarget) return;
    if (phase === 'stopped') return start(completed, target);
    if (phase !== 'running') return;
    pendingTarget = target;
    setPhase('stopping', 'Saving card and switching target…');
    link.clear(); runtime._web_stop();
});
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
window.xtgvWeb = {get phase() {return phase;}, snapshot: getSnapshot, get target() {return selectedTarget;}, get profiles() {return profiles.map(p => structuredClone(p));}};
(async () => {
    if (!isSecureContext) throw Error('This page needs a secure browser context to start RunCPM. Use localhost on this computer, or HTTPS when connecting over the LAN; HTTP to a LAN IP is not secure enough.');
    if (!crossOriginIsolated) throw Error('The server is missing COOP/COEP isolation headers required by the RunCPM worker.');
    const response = await fetch('card.zip');
    if (!response.ok) throw Error('Preload card is missing. Load a card ZIP to continue.');
    await start(unpackCard(new Uint8Array(await response.arrayBuffer())));
})().catch(fail);
