// One ordered RX port. Complete frames enter the C++ serial FIFO atomically.
export class SerialLink {
    constructor() {this.queue = []; this.size = 0; this.runtime = null;}
    attach(runtime) {this.clear(); this.runtime = runtime;}
    clear() {this.queue = []; this.size = 0; this.runtime = null;}
    send(bytes) {
        if (!this.runtime) throw Error('Keyboard is waiting for the runtime.');
        if (this.size + bytes.length > 8192) throw Error('Input queue full; wait before sending more text.');
        // Text/paste can be split; private key frames (<=64 bytes) stay whole.
        for (let i = 0; i < bytes.length; i += 64) this.queue.push(bytes.slice(i, i + 64));
        this.size += bytes.length;
        this.pump();
    }
    pump() {
        if (!this.runtime) return;
        while (this.queue.length) {
            const bytes = this.queue[0];
            // Capacity and enqueue are checked atomically by the UI-owned C++ shim.
            const used = this.runtime.ccall('web_receive_event', 'number', ['array', 'number'], [bytes, bytes.length]);
            if (!used) break;
            if (used !== bytes.length) throw Error('Serial frame was not accepted atomically.');
            this.size -= used; this.queue.shift();
        }
    }
    cancelPending() {this.queue = []; this.size = 0;}
}
