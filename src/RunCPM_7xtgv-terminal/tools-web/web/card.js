// Browser-local card ZIPs. Files stay in MEMFS until an explicit download.
import {unzipSync, zipSync} from './vendor/fflate.js';
export const CARD_LIMIT = 32 * 1024 * 1024;
const ENTRY_LIMIT = 10000;
function pathName(name) {
    const value = name.endsWith('/') ? name.slice(0, -1) : name;
    if (!value || /[\\\x00-\x1f:]/.test(value) || value.startsWith('/') ||
        value.split('/').some(p => !p || p === '.' || p === '..' || p.toLowerCase() === 'private' || p === '.7xtgv-desktop-lock'))
        throw Error('Unsupported card path: ' + name);
    return value;
}
export function unpackCard(bytes) {
    if (bytes.length > CARD_LIMIT) throw Error('Card ZIP exceeds 32 MiB.');
    let total = 0, count = 0;
    const names = new Set();
    const entries = unzipSync(bytes, {filter: info => {
        const name = pathName(info.name);
        if (names.has(name)) throw Error('Duplicate card path: ' + name);
        names.add(name);
        total += info.originalSize;
        if (info.name !== 'cpmcard/') ++count;
        if (count > ENTRY_LIMIT || total > CARD_LIMIT) throw Error('Card exceeds 32 MiB or 10000 entries.');
        return true;
    }});
    const directories = [], files = Object.create(null);
    // Keep accepting earlier exports with A/0 directly at the ZIP root.
    const wrapped = Object.keys(entries).some(name => name.startsWith('cpmcard/'));
    for (const [name, bytes] of Object.entries(entries)) {
        if (wrapped && name === 'cpmcard/') continue;
        if (wrapped && !name.startsWith('cpmcard/'))
            throw Error('Card ZIP entries must all be inside cpmcard/.');
        const path = pathName(wrapped ? name.slice('cpmcard/'.length) : name);
        if (name.endsWith('/')) directories.push(path);
        else files[path] = bytes;
    }
    if (!directories.includes('A/0') && !Object.keys(files).some(n => n.startsWith('A/0/')))
        throw Error('Card ZIP must contain cpmcard/A/0/ (or legacy A/0/).');
    // Detect file/directory conflicts before touching a running filesystem.
    const all = [...directories, ...Object.keys(files)];
    for (const name of all) {
        const parts = name.split('/'); parts.pop();
        while (parts.length) {
            if (Object.hasOwn(files, parts.join('/'))) throw Error('File conflicts with directory: ' + name);
            parts.pop();
        }
    }
    return {directories, files};
}
export function installCard(FS, card) {
    FS.mkdir('/card');
    for (const name of card.directories) FS.mkdirTree('/card/' + name);
    for (const [name, bytes] of Object.entries(card.files)) {
        const path = '/card/' + name;
        FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
        FS.writeFile(path, bytes);
    }
}
export function snapshotCard(FS) {
    const directories = [], files = Object.create(null);
    let total = 0, count = 0;
    function walk(path, relative) {
        for (const name of FS.readdir(path)) {
            if (name === '.' || name === '..') continue;
            const full = path + '/' + name, key = pathName(relative ? relative + '/' + name : name);
            if (++count > ENTRY_LIMIT) throw Error('Card exceeds 10000 entries.');
            const stat = FS.lstat(full);
            if (FS.isDir(stat.mode)) {directories.push(key); walk(full, key);}
            else if (FS.isFile(stat.mode)) {
                total += stat.size;
                if (total > CARD_LIMIT) throw Error('Card exceeds 32 MiB.');
                files[key] = FS.readFile(full);
            } else throw Error('Unsupported card entry: ' + key);
        }
    }
    walk('/card', '');
    return {directories, files};
}
export function packCard(card) {
    const entries = Object.create(null);
    entries['cpmcard/'] = new Uint8Array();
    for (const name of card.directories) entries['cpmcard/' + pathName(name) + '/'] = new Uint8Array();
    for (const [name, bytes] of Object.entries(card.files)) entries['cpmcard/' + pathName(name)] = bytes;
    return new Blob([zipSync(entries, {level: 1})], {type: 'application/zip'});
}
