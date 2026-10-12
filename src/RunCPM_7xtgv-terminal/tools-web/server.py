#!/usr/bin/env python3
"""Local, experimental browser runtime owner. No host card writes."""
import argparse
import asyncio
import base64
import io
import json
import os
from pathlib import Path, PurePosixPath
import secrets
import signal
import stat
import tempfile
import zipfile
from aiohttp import web

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / 'build/RunCPM_7xtgv-terminal/desktop-web'
LIMIT = 32 * 1024 * 1024


def card_path(name):
    path = PurePosixPath(name)
    if (not name or '\\' in name or path.is_absolute() or
            any(part in ('..', '.', 'private') for part in path.parts) or
            '\x00' in name):
        raise ValueError('Invalid card path')
    return str(path)


def snapshot(root):
    """Read a private snapshot; skip private directories and ownership metadata."""
    files, directories, total = {}, [], 0
    for folder, dirs, names in os.walk(root, followlinks=False):
        dirs[:] = sorted(d for d in dirs if d not in ('private', '.7xtgv-desktop-lock')
                         and not (Path(folder) / d).is_symlink())
        relative = Path(folder).relative_to(root)
        if relative.parts:
            directories.append(card_path(relative.as_posix()))
        for name in sorted(names):
            path = Path(folder) / name
            if path.is_symlink() or not path.is_file() or name.startswith('.xtgv'):
                continue
            content = path.read_bytes()
            total += len(content)
            if total > LIMIT:
                raise ValueError('Proof card exceeds 32 MiB')
            files[card_path((relative / name).as_posix())] = base64.b64encode(content).decode('ascii')
    return {'directories': directories, 'files': files}


def unpack(data):
    files, directories, total = {}, [], 0
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        if len(archive.infolist()) > 10000:
            raise ValueError('Too many card entries')
        for info in archive.infolist():
            name = card_path(info.filename.rstrip('/'))
            if stat.S_ISLNK(info.external_attr >> 16):
                raise ValueError('ZIP symlinks are unsupported')
            total += info.file_size
            if total > LIMIT:
                raise ValueError('Proof card exceeds 32 MiB')
            if info.is_dir():
                directories.append(name)
            else:
                files[name] = base64.b64encode(archive.read(info)).decode('ascii')
    if 'A/0' not in directories and not any(n.startswith('A/0/') for n in files):
        raise ValueError('ZIP must contain A/0 at its root')
    return {'directories': directories, 'files': files}


def pack(card):
    out, total = io.BytesIO(), 0
    entries = card['directories'] + list(card['files'])
    if len(entries) > 10000:
        raise ValueError('Too many card entries')
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in card['directories']:
            archive.writestr(card_path(name) + '/', b'')
        for name, encoded in card['files'].items():
            value = base64.b64decode(encoded, validate=True)
            total += len(value)
            if total > LIMIT:
                raise ValueError('Proof card exceeds 32 MiB')
            archive.writestr(card_path(name), value)
    return out.getvalue()


class Owner:
    def __init__(self, args, session):
        self.args, self.session = args, session
        self.token = secrets.token_hex(32)
        self.card = unpack(args.card_zip.read_bytes()) if args.card_zip else snapshot(args.card_root)
        self.ws = self.writer = None
        self.ready = asyncio.Event()
        self.closed = asyncio.Event()
        self.finished = False
        self.status = 'waiting-for-browser'
        self.output = (session / 'mirror.bin').open('wb')
        self.input = (session / 'input.bin').open('wb')

    async def tcp(self, reader, writer):
        accepted = False
        try:
            auth = await asyncio.wait_for(reader.readline(), 5)
            if auth != ('XTGV/1 ' + self.token + '\n').encode() or self.writer is not None:
                return
            await asyncio.wait_for(self.ready.wait(), 60)
            if self.finished or self.ws is None:
                return
            self.writer, accepted = writer, True
            await self.ws.send_json({'mirror': True})
            while data := await reader.read(4096):
                self.input.write(data)
                self.input.flush()
                await self.ws.send_bytes(data)
        except (ConnectionError, asyncio.TimeoutError):
            pass
        finally:
            if accepted:
                self.writer = None
                if self.ws is not None and not self.ws.closed and not self.finished:
                    await self.ws.send_json({'stop': True})
            writer.close()
            await writer.wait_closed()

    async def socket(self, request):
        # Token and same-origin checks prevent unrelated pages controlling this session.
        if request.headers.get('Origin') != self.origin or self.ws is not None:
            raise web.HTTPForbidden()
        ws = web.WebSocketResponse(max_msg_size=65536, heartbeat=15)
        await ws.prepare(request)
        self.ws = ws
        try:
            async for message in ws:
                if message.type == web.WSMsgType.BINARY:
                    self.output.write(message.data)
                    self.output.flush()
                    if self.writer:
                        self.writer.write(message.data)
                        await self.writer.drain()
                elif message.type == web.WSMsgType.TEXT:
                    data = json.loads(message.data)
                    if data.get('ready'):
                        self.status = 'running'
                        self.ready.set()
                    if 'finished' in data:
                        self.finished = True
                        self.status = 'finished' if data['finished'] == 0 else 'failed'
                        (self.session / 'result.json').write_text(json.dumps(data, indent=2))
                        if self.writer:
                            self.writer.close()
                    if 'error' in data:
                        print('Browser:', data['error'], flush=True)
                    if data.get('reboot'):
                        print('Guest REBOOT: new Wasm instance, same in-memory card.', flush=True)
                elif message.type == web.WSMsgType.ERROR:
                    print('WebSocket error:', ws.exception(), flush=True)
        finally:
            if self.writer:
                self.writer.close()
            self.ws = None
            self.closed.set()
        return ws

    async def resource(self, request):
        name = request.match_info['name']
        if name == 'card':
            return web.json_response(self.card)
        if name == 'status':
            return web.json_response({'status': self.status})
        if name in ('', 'index.html', 'client.js'):
            return web.FileResponse(HERE / 'web' / (name or 'index.html'))
        if name in ('xtgv_web.js', 'xtgv_web.wasm', 'xtgv_web.worker.js'):
            return web.FileResponse(BUILD / 'shared' / name)
        raise web.HTTPNotFound()

    async def export(self, request):
        if not self.finished or self.status != 'finished':
            raise web.HTTPConflict(text='Stop the guest successfully before export.')
        try:
            data = pack(await request.json())
        except (ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
            raise web.HTTPBadRequest(text=str(error)) from error
        (self.session / 'exported-card.zip').write_bytes(data)
        return web.Response(body=data, content_type='application/zip',
                            headers={'Content-Disposition': 'attachment; filename="cpmcard-web.zip"'})


@web.middleware
async def isolation(request, handler):
    try:
        response = await handler(request)
    except web.HTTPException as error:
        response = error
    response.headers.update({'Cross-Origin-Opener-Policy': 'same-origin',
                             'Cross-Origin-Embedder-Policy': 'require-corp',
                             'Cross-Origin-Resource-Policy': 'same-origin',
                             'Cache-Control': 'no-store'})
    return response


async def serve(args):
    (BUILD / 'sessions').mkdir(parents=True, exist_ok=True)
    session = args.session_dir or Path(tempfile.mkdtemp(prefix='web-', dir=BUILD / 'sessions'))
    session.mkdir(parents=True, exist_ok=True)
    session.chmod(0o700)
    owner = Owner(args, session)
    app = web.Application(middlewares=[isolation], client_max_size=48 * 1024 * 1024)
    base = '/' + owner.token + '/'
    app.router.add_get(base + 'socket', owner.socket)
    app.router.add_post(base + 'export', owner.export)
    app.router.add_get(base + '{name:.*}', owner.resource)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, '127.0.0.1', 0)
    await site.start()
    owner.origin = 'http://127.0.0.1:' + str(site._server.sockets[0].getsockname()[1])
    tcp = await asyncio.start_server(owner.tcp, '127.0.0.1', 0, limit=65536)
    metadata = {'url': owner.origin + base, 'port': tcp.sockets[0].getsockname()[1],
                'token': owner.token, 'session': str(session), 'scale': args.window_scale}
    (session / 'session.json').write_text(json.dumps(metadata, indent=2))
    print('WEB_SESSION=' + str(session), flush=True)
    print('WEB_URL=' + metadata['url'], flush=True)
    print('Card snapshot ready; the host card will not be modified.', flush=True)
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, owner.closed.set)
    try:
        await owner.closed.wait()
    finally:
        if owner.ws is not None and not owner.ws.closed:
            await owner.ws.send_json({'stop': True})
            for _ in range(50):
                if owner.finished:
                    break
                await asyncio.sleep(0.1)
        tcp.close()
        await tcp.wait_closed()
        await runner.cleanup()
        owner.output.close()
        owner.input.close()
        (session / 'status.json').write_text(json.dumps({'status': owner.status}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--card-root', type=Path, default=ROOT / 'cpmcard')
    parser.add_argument('--card-zip', type=Path)
    parser.add_argument('--session-dir', type=Path)
    parser.add_argument('--window-scale', type=int, choices=range(1, 10), default=2)
    asyncio.run(serve(parser.parse_args()))


if __name__ == '__main__':
    main()
