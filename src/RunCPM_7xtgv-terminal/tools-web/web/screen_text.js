import {download} from './display.js';
// Frozen observer: no key handlers and no access to terminal input/state mutation.
export class ScreenText {
    constructor(panel, text, label, snapshot) {
        this.panel = panel; this.text = text; this.label = label; this.snapshot = snapshot;
        this.current = '';
    }
    refresh() {
        const s = this.snapshot();
        this.current = s.text;
        this.text.textContent = s.text;
        this.label.textContent = `${s.cols} × ${s.rows} logical text cells · frozen ${new Date().toLocaleTimeString()} · graphics are available in PNG`;
        this.panel.hidden = false;
    }
    copy() {
        if (!navigator.clipboard?.writeText) throw Error('Clipboard unavailable; select the text or download it.');
        return navigator.clipboard.writeText(this.current);
    }
    download() {download(new Blob([this.current], {type:'text/plain;charset=utf-8'}), '7xtgv-screen.txt');}
}
