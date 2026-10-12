'use strict';
const statusLine = document.querySelector('#status');
const canvas = document.querySelector('#display');
const context = canvas.getContext('2d', {alpha: false});
const scale = Math.max(1, Math.min(9, Number(new URLSearchParams(location.search).get('scale')) || 2));
const socket = new WebSocket(new URL('socket', location.href).href.replace('http:', 'ws:'));
socket.binaryType = 'arraybuffer';
let runtime, stopped = false, connected = false, pending = [], pendingBytes = 0, card;
let image;
const opened = new Promise((resolve, reject) => {socket.onopen = resolve; socket.onerror = reject;});
function fail(error) {
    statusLine.textContent = 'Failed: ' + error;
    console.error(error);
    if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({error: String(error)}));
}
function snapshot() {
    const result = {directories: [], files: {}};
    function walk(path, relative) {
        for (const name of runtime.FS.readdir(path)) {
            if (name === '.' || name === '..') continue;
            const full = path + '/' + name, key = relative ? relative + '/' + name : name;
            if (runtime.FS.isDir(runtime.FS.stat(full).mode)) {
                result.directories.push(key); walk(full, key);
            } else {
                const bytes = runtime.FS.readFile(full);
                let text = '';
                for (let i = 0; i < bytes.length; i += 8192)
                    text += String.fromCharCode(...bytes.subarray(i, i + 8192));
                result.files[key] = btoa(text);
            }
        }
    }
    walk('/card', '');
    return result;
}
async function start(contents) {
    runtime = await createXtgv({noInitialRun: true,
        print: console.log, printErr: console.error,
        onAbort: fail,
        mirrorReady: () => socket.readyState === WebSocket.OPEN && socket.bufferedAmount < 65536,
        mirrorOutput: bytes => socket.send(bytes),
        presentFrame: (pixels, width, height, brightness) => {
            if (!image || canvas.width !== width || canvas.height !== height) {
                canvas.width = width; canvas.height = height;
                canvas.style.width = width * scale + 'px'; canvas.style.height = height * scale + 'px';
                image = context.createImageData(width, height);
            }
            const out = image.data, factor = brightness / 255;
            for (let i = 0; i < pixels.length; ++i) {
                const c = pixels[i], at = i * 4;
                out[at] = ((c >> 11) & 31) * 255 / 31 * factor;
                out[at + 1] = ((c >> 5) & 63) * 255 / 63 * factor;
                out[at + 2] = (c & 31) * 255 / 31 * factor; out[at + 3] = 255;
            }
            context.putImageData(image, 0, 0);
        },
        guestFinished: (code, error) => {
            if (code === 75) {
                const saved = snapshot();
                runtime.PThread.terminateAllThreads();
                socket.send(JSON.stringify({reboot: true}));
                start(saved).catch(fail);
                return;
            }
            stopped = true;
            document.querySelector('#stop').disabled = true;
            document.querySelector('#export').disabled = code !== 0;
            statusLine.textContent = code ? 'Stopped with an error: ' + error : 'Stopped cleanly. The card is ready to download.';
            socket.send(JSON.stringify({finished: code, error}));
        }
    });
    runtime.FS.mkdir('/card');
    for (const name of contents.directories) runtime.FS.mkdirTree('/card/' + name);
    for (const [name, encoded] of Object.entries(contents.files)) {
        const path = '/card/' + name;
        runtime.FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
        runtime.FS.writeFile(path, Uint8Array.from(atob(encoded), c => c.charCodeAt(0)));
    }
    runtime._web_connect(connected ? 1 : 0);
    runtime.callMain([]);
    socket.send(JSON.stringify({ready: true}));
    document.querySelector('#stop').disabled = false;
    statusLine.textContent = connected ? 'Running · keyboard input belongs to the native terminal.' : 'Running · waiting for the native terminal mirror.';
    // Exposed only to the local proof tests; never handles browser keyboard input.
    window.webProof = {runtime, snapshot, get stopped() {return stopped;}};
}
socket.onmessage = event => {
    if (typeof event.data === 'string') {
        const message = JSON.parse(event.data);
        if (message.mirror) {
            connected = true; runtime?._web_connect(1);
            statusLine.textContent = 'Running · keyboard input belongs to the native terminal.';
        }
        if (message.stop) runtime?._web_stop();
    } else {
        pending.push(new Uint8Array(event.data)); pendingBytes += event.data.byteLength;
        if (pendingBytes > 65536) { fail('Mirror input queue exceeded 64 KiB'); runtime?._web_stop(); socket.close(); }
    }
};
socket.onclose = () => { if (!stopped) {runtime?._web_stop(); statusLine.textContent = 'Bridge closed; stopping the guest.';} };
setInterval(() => {
    if (!runtime || stopped) return;
    while (pending.length) {
        const bytes = pending[0];
        const used = runtime.ccall('web_receive', 'number', ['array', 'number'], [bytes, bytes.length]);
        pendingBytes -= used;
        if (used === bytes.length) pending.shift();
        else {pending[0] = bytes.subarray(used); break;}
    }
}, 5);
document.querySelector('#stop').onclick = () => runtime._web_stop();
document.querySelector('#export').onclick = async () => {
    try {
        const response = await fetch('export', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(snapshot())});
        if (!response.ok) throw new Error(await response.text());
        const url = URL.createObjectURL(await response.blob());
        const link = document.createElement('a'); link.href = url; link.download = 'cpmcard-web.zip'; link.click();
        setTimeout(() => URL.revokeObjectURL(url), 5000);
    } catch (error) {fail(error);}
};
(async () => {
    if (!crossOriginIsolated) throw new Error('This browser needs cross-origin isolation for the guest worker.');
    await opened;
    card = await (await fetch('card')).json();
    await start(card);
})().catch(fail);
