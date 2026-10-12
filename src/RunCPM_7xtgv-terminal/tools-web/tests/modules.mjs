// Boundary checks: whole-frame flow control, card validation and layout guards.
import assert from 'node:assert/strict';
const site = new URL('../../../../build/RunCPM_7xtgv-terminal/desktop-web/site/', import.meta.url);
const {SerialLink} = await import(new URL('serial_link.js', site));
const {encodeKey, keyEvent} = await import(new URL('input.js', site));
const {unpackCard, packCard} = await import(new URL('card.js', site));
const {zipSync, unzipSync} = await import(new URL('vendor/fflate.js', site));
const wire = new TextDecoder();
const link = new SerialLink();
let capacity = 0;
const accepted = [];
link.attach({ccall(name, result, types, [bytes, count]) {
    assert.equal(name, 'web_receive_event');
    if (count > capacity) return 0;
    accepted.push(bytes.slice()); capacity -= count; return count;
}});
const first = keyEvent('KeyLeft', 0, '\x1b[D');
const second = keyEvent('KeyRight', 0, '\x1b[C');
link.send(first); link.send(second);
capacity = first.length - 1; link.pump();
assert.equal(accepted.length, 0, 'No partial key frame');
capacity = first.length; link.pump();
assert.equal(accepted.length, 1); assert.deepEqual(accepted[0], first);
link.cancelPending(); capacity = 100; link.pump();
assert.equal(accepted.length, 1, 'Blur cancels wholly unsent frames');
capacity = 0; link.send(second);
link.attach({ccall() {throw Error('Stale frame entered new runtime');}});
link.pump();
assert.throws(() => link.send(new Uint8Array(8193)), /queue full/);
link.clear(); assert.throws(() => link.send(first), /waiting/);
const event = (key, code, more = {}) => ({key, code, ctrlKey:false,altKey:false,shiftKey:false,...more});
assert.equal(encodeKey(event('ě','Digit2'), ''), null);
assert.equal(encodeKey(event('é','Digit2'), '+ěščřžýáíé'), null);
assert.equal(encodeKey(event('c','KeyC',{ctrlKey:true,altKey:true,altGraph:true})), null);
assert.equal(encodeKey(event('Dead','Quote')), null);
assert.equal(wire.decode(encodeKey(event('#','Digit3',{altKey:true,shiftKey:true}))), '\x1b[>15v');
const original = {directories:['A','A/0','B','B/1'],files:{'A/0/VTBIN.COM':new Uint8Array([0,255,26,0,128])}};
assert.deepEqual(unpackCard(new Uint8Array(await packCard(original).arrayBuffer())).files['A/0/VTBIN.COM'], original.files['A/0/VTBIN.COM']);
assert(unpackCard(new Uint8Array(await packCard(original).arrayBuffer())).directories.includes('B/1'));
const wrapped = new Uint8Array(await packCard(original).arrayBuffer());
assert(Object.keys(unzipSync(wrapped)).every(n => n.startsWith('cpmcard/')));
assert.deepEqual(unzipSync(wrapped)['cpmcard/A/0/VTBIN.COM'], original.files['A/0/VTBIN.COM']);
const legacy = zipSync({'A/0/VTBIN.COM':original.files['A/0/VTBIN.COM'], 'B/1/':new Uint8Array()});
assert.deepEqual(unpackCard(legacy).files, unpackCard(wrapped).files);
assert(unpackCard(legacy).directories.includes('B/1'));
// Root directory entries are optional in ZIPs made by other tools.
assert.deepEqual(unpackCard(zipSync({'cpmcard/A/0/VTBIN.COM':original.files['A/0/VTBIN.COM']})).files['A/0/VTBIN.COM'], original.files['A/0/VTBIN.COM']);
assert.throws(() => unpackCard(zipSync({'cpmcard/A/0/':new Uint8Array(), 'A/0/':new Uint8Array()})), /inside cpmcard/);
assert.throws(() => unpackCard(zipSync({'cpmcard/A/0/':new Uint8Array(), 'cpmcard/../outside':new Uint8Array()})), /path/);

for (const bad of ['../outside','/absolute','A/../bad','A/private/file','A/0/C:\\file']) {
    const zip = zipSync({'A/0/':new Uint8Array(),[bad]:new Uint8Array([1])});
    assert.throws(() => unpackCard(zip), /path/);
}
assert.throws(() => unpackCard(zipSync({'A/0/file':new Uint8Array(),'A/0/file/child':new Uint8Array()})), /conflicts/);
assert.throws(() => unpackCard(zipSync({'A/0/':new Uint8Array(),'A/0':new Uint8Array()})), /Duplicate/);
assert.throws(() => unpackCard(zipSync({'wrong-root/':new Uint8Array()})), /A\/0/);
assert.throws(() => unpackCard(new Uint8Array(32*1024*1024+1)), /32 MiB/);
console.log('PASS: whole-frame backpressure/cancellation/epoch/limits; layout guards; binary ZIP round trip and invalid paths.');
