#!/usr/bin/env python3
"""Serve a standalone static bundle with worker isolation headers. No guest bridge."""
import argparse
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import webbrowser
from urllib.parse import urlencode
from build_assets import BUILD, package


class StaticHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Resource-Policy', 'same-origin')
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def list_directory(self, path):
        self.send_error(403)
        return None


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--window-scale', type=int, choices=range(1, 10), default=2)
    p.add_argument('--target', default='jc8048w535', help='Display ID or board target; also selectable in the page')
    p.add_argument('--card-root', type=Path)
    p.add_argument('--card-zip', type=Path)
    p.add_argument('--session-dir', type=Path)
    p.add_argument('--no-open', action='store_true')
    a = p.parse_args()
    (BUILD / 'sessions').mkdir(parents=True, exist_ok=True)
    session = a.session_dir or Path(tempfile.mkdtemp(prefix='standalone-', dir=BUILD / 'sessions'))
    session.mkdir(parents=True, exist_ok=True)
    session.chmod(0o700)
    site = session / 'site'
    package(site, a.card_root, a.card_zip)
    handler = functools.partial(StaticHandler, directory=str(site))
    with ThreadingHTTPServer(('127.0.0.1', 0), handler) as server:
        url = f'http://127.0.0.1:{server.server_port}/?' + urlencode({'scale': a.window_scale, 'target': a.target})
        (session / 'session.json').write_text(json.dumps({'url':url, 'session':str(session)}, indent=2))
        print(f'*** WEB · {a.target} · standalone ***', flush=True)
        print('Browser keyboard, private memory card, native C++ terminal.', flush=True)
        print('URL:', url, flush=True)
        print('Evidence:', session, flush=True)
        print('Stop and download the card in the page. Ctrl+C here ends static hosting.', flush=True)
        if not a.no_open:
            webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
