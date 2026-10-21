// Browser keyboard module. Its only guest-facing capability is send(Uint8Array).
import {keyIds, defaultTopRow} from './protocol.js';
const utf8 = new TextEncoder();
const csi = text => utf8.encode('\x1b[' + text);
const levels = [0, 4, 7, 11, 14, 18, 21, 25, 28, 31];
const navigation = {
    ArrowLeft: ['KeyLeft', 'D'], ArrowRight: ['KeyRight', 'C'],
    ArrowUp: ['KeyUp', 'A'], ArrowDown: ['KeyDown', 'B'],
    Home: ['KeyHome', 'H'], End: ['KeyEnd', 'F'],
    Insert: ['KeyInsert', 2], Delete: ['KeyDelete', 3],
    PageUp: ['KeyPageUp', 5], PageDown: ['KeyPageDown', 6],
};
export function keyEvent(base, mods, fallback = '') {
    const bytes = utf8.encode(fallback);
    if (!keyIds[base] || bytes.length > 16) throw Error('Invalid host key event');
    return csi(`>70;1;${keyIds[base]};${mods};${Array.from(bytes, b => String(b).padStart(3, '0')).join('')}v`);
}
function xterm(code, mods) {
    const modifier = 1 + !!(mods & 2) + 2 * !!(mods & 4) + 4 * !!(mods & 1);
    if (typeof code === 'number') return `\x1b[${code}${mods ? ';' + modifier : ''}~`;
    return `\x1b[${mods ? '1;' + modifier : ''}${code}`;
}
// null leaves ordinary text/composition or a browser shortcut to its normal path.
export function encodeKey(e, topRow = defaultTopRow) {
    if (e.metaKey || e.isComposing || e.key === 'Dead' || e.key === 'Process' || e.altGraph) return null;
    const mods = (e.ctrlKey ? 1 : 0) | (e.shiftKey ? 2 : 0) | (e.altKey ? 4 : 0);
    const digitMatch = /^(?:Digit|Numpad)([0-9])$/.exec(e.code);
    const digit = digitMatch ? Number(digitMatch[1]) : null;
    if (digit !== null) {
        if (mods === 7 && digit === 0) return csi('>63;0v');
        if (mods === 7 && (digit === 1 || digit === 2)) return csi(`>${digit}v`);
        if (mods === 3 && digit) return csi(`>63;${levels[digit]}v`);
        if (mods === 4) return csi(`>2${digit}v`);
        const rowChar = Array.from(topRow)[(digit + 9) % 10];
        if ((mods === 0 || mods === 2) && (e.key === String(digit) ||
            (mods === 0 && rowChar && e.key === rowChar)))
            return keyEvent('KeyNum' + digit, mods, e.key);
    }
    if (e.altKey && !e.ctrlKey && e.key === '#') return csi('>15v');
    // Browser aliases retain the existing terminal commands/profile bindings.
    if (e.key === ' ' && mods === 1) return csi('>14v');
    if (mods === 4) {
        const global = {q: '>60v', Q: '>60v', Escape: '>60v', Backspace: '>61v',
            ' ': '>14v', PageUp: '>12v', PageDown: '>13v', '#': '>15v'};
        if (global[e.key]) return csi(global[e.key]);
    }
    if (e.key === 'PageUp' || e.key === 'PageDown') {
        if (mods === 5) {
            const [base, code] = navigation[e.key];
            return keyEvent(base, 1, xterm(code, 1));
        }
        if (mods === 3) return csi(e.key === 'PageUp' ? '>67v' : '>66v');
        if (mods === 7) return csi(e.key === 'PageUp' ? '>63;33v' : '>63;34v');
    }
    if (e.key === 'Backspace' && mods === 3) return csi('>65v');
    if (e.key.toLowerCase() === 'c' && mods === 5) return keyEvent('KeyC', 5);
    // Remaining Ctrl+Alt text belongs to national layouts, not guessed shortcuts.
    if (mods === 5 || mods === 7) {
        if (!navigation[e.key]) return null;
    }
    if (e.code === 'NumpadDecimal' && mods === 0) {
        return keyEvent(e.key === 'Delete' ? 'KeyKpDelete' : 'KeyKpDecimal', 0,
                        e.key === 'Delete' ? '\x1b[3~' : e.key);
    }
    if (navigation[e.key]) {
        const [base, code] = navigation[e.key];
        return keyEvent(base, mods, xterm(code, mods));
    }
    const fn = /^F([1-9]|1[0-2])$/.exec(e.key);
    if (fn) {
        const n = Number(fn[1]);
        if (n === 11 && mods === 0) return null;
        const code = ['P', 'Q', 'R', 'S', 15, 17, 18, 19, 20, 21, 23, 24][n - 1];
        return keyEvent('KeyF' + n, mods, n <= 4 && !mods ? '\x1bO' + code : xterm(code, mods));
    }
    const special = {Backspace: ['KeyBackspace', mods & 1 ? '\x18' : '\x08'],
        Enter: ['KeyEnter', '\r'], Escape: ['KeyEscape', '\x1b'],
        ' ': ['KeySpace', mods & 1 ? '\0' : ' ']};
    if (special[e.key]) {
        const [base, fallback] = special[e.key];
        return keyEvent(base, mods, (mods & 4 ? '\x1b' : '') + fallback);
    }
    if (e.key === 'Tab') return utf8.encode(mods === 2 ? '\x1b[Z' : '\t');
    if (/^[a-z]$/i.test(e.key) && mods & 5) {
        const letter = e.key.toUpperCase();
        const fallback = mods & 1 ? String.fromCharCode(letter.charCodeAt(0) - 64) : e.key;
        return keyEvent('Key' + letter, mods, (mods & 4 ? '\x1b' : '') + fallback);
    }
    // Preserve the established, tested Czech punctuation bindings and fallback.
    if (mods === 0 || mods === 2) {
        const punctuation = { '=': 'KeyBs2', '%': 'KeyBs2', 'ú': 'KeyP1', '/': 'KeyP1',
            ')': 'KeyP2', '(': 'KeyP2', 'ů': 'KeyL1', '§': 'KeyL2', '!': 'KeyL2',
            ',': 'KeyM1', '?': 'KeyM1', '.': 'KeyM2', ':': 'KeyM2', '-': 'KeyM3', '_': 'KeyM3'};
        if (punctuation[e.key]) return keyEvent(punctuation[e.key], mods, e.key);
    }
    if (e.ctrlKey && e.key.length === 1) {
        const code = e.key.charCodeAt(0);
        if (code >= 64 && code <= 95) return new Uint8Array([code - 64]);
    }
    return null;
}

export class KeyboardModule {
    constructor(area, input, send, report, config) {
        this.enabled = false;
        this.composing = false;
        this.rightAlt = false;
        this.stickyModifiers = 0;
        this.onStickyModifiersChange = () => {};
        this.topRow = config ?? defaultTopRow;
        this.input = input;
        this.send = send;
        this.report = report;
        area.addEventListener('pointerdown', e => {
            if (!this.enabled || e.button !== 0) return;
            // The default mouse action would blur our input again after this
            // handler focuses it, because the visible canvas is not editable.
            e.preventDefault();
            this.focus();
        });
        input.addEventListener('keydown', e => {
            if (e.code === 'AltRight') this.rightAlt = true;
            if (!this.enabled || this.composing || e.isComposing) return;
            const event = this.event(e), sticky = this.stickyModifiers;
            const touchTextEntry = sticky && event.key.length === 1 &&
                globalThis.matchMedia?.('(pointer: coarse)').matches &&
                !event.ctrlKey && !event.altKey && !event.metaKey;
            if (touchTextEntry) return;
            const bytes = encodeKey({...event,
                ctrlKey: event.ctrlKey || !!(sticky & 1),
                shiftKey: event.shiftKey || !!(sticky & 2),
                altKey: event.altKey || !!(sticky & 4),
                altGraph: this.rightAlt || e.getModifierState('AltGraph')}, this.topRow);
            if (bytes) {
                e.preventDefault(); this.deliver(bytes);
                if (sticky) this.setStickyModifiers(0);
            }
        });
        input.addEventListener('keyup', e => {if (e.code === 'AltRight') this.rightAlt = false;});
        input.addEventListener('compositionstart', () => {this.composing = true;});
        input.addEventListener('compositionend', () => {
            this.composing = false;
            // Read the committed value once, after the final input event.
            queueMicrotask(() => this.commit());
        });
        input.addEventListener('input', e => {if (!this.composing && !e.isComposing) this.commit();});
        input.addEventListener('paste', e => {
            e.preventDefault();
            if (this.enabled) this.text(e.clipboardData.getData('text/plain'));
        });
        input.addEventListener('blur', () => {
            this.rightAlt = this.composing = false; input.value = '';
            area.classList.remove('captured');
        });
        input.addEventListener('focus', () => {area.classList.add('captured'); report('');});
    }
    event(e) {
        return {key: e.key, code: e.code, ctrlKey: e.ctrlKey, altKey: e.altKey,
            shiftKey: e.shiftKey, metaKey: e.metaKey, isComposing: e.isComposing};
    }
    focus() { if (this.enabled) this.input.focus({preventScroll: true}); }
    setStickyModifiers(value) {
        this.stickyModifiers = value & 7;
        this.onStickyModifiersChange(this.stickyModifiers);
    }
    touchKey(key, code, modifiers) {
        if (!this.enabled) return false;
        const bytes = encodeKey({key, code, ctrlKey: !!(modifiers & 1),
            altKey: !!(modifiers & 4), shiftKey: !!(modifiers & 2),
            metaKey: false, isComposing: false, altGraph: false}, this.topRow);
        if (!bytes) return false;
        this.deliver(bytes);
        return true;
    }
    deliver(bytes) {try {this.send(bytes);} catch (error) {this.report(error.message);}}
    commit() {
        if (this.composing) return;
        const text = this.input.value;
        this.input.value = '';
        if (text && this.enabled && document.activeElement === this.input) this.text(text, true);
    }
    text(value, applyStickyModifiers = false) {
        const text = value.replace(/\r\n?/g, '\n').replace(/\n/g, '\r');
        if (text.length > 4096) {this.report('Paste limit: 4096 characters.'); return;}
        if (/[^\x20-\x7e\t\r]/.test(text)) {
            this.report('Plain guest text accepts ASCII, Tab and newlines. Unsupported text was not sent.'); return;
        }
        if (text && applyStickyModifiers && this.stickyModifiers) {
            const first = text[0], modifiers = this.stickyModifiers;
            const code = /^[a-z]$/i.test(first) ? 'Key' + first.toUpperCase() :
                /^[0-9]$/.test(first) ? 'Digit' + first : '';
            const bytes = encodeKey({key:first, code, ctrlKey:!!(modifiers & 1),
                shiftKey:!!(modifiers & 2), altKey:!!(modifiers & 4),
                metaKey:false, isComposing:false, altGraph:false}, this.topRow);
            this.setStickyModifiers(0);
            if (bytes) this.deliver(bytes);
            else if (modifiers === 2 && /^[a-z]$/i.test(first))
                this.deliver(utf8.encode(first.toUpperCase()));
            else this.deliver(utf8.encode(first));
            if (text.length > 1) this.text(text.slice(1));
            return;
        }
        this.deliver(utf8.encode(text));
    }
    setEnabled(value) {
        this.enabled = value; this.input.disabled = !value;
        if (!value) {
            this.input.value = ''; this.rightAlt = this.composing = false;
            this.setStickyModifiers(0);
        }
    }
}
