#!/usr/bin/env python3
"""Package or preview a GitHub Pages site. Never deploys or modifies the working card."""
import argparse
import functools
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import tempfile
import webbrowser

from build_assets import BUILD, HERE, ROOT, package

OUTPUT = BUILD / 'pages'


def committed_card(destination):
    """Use Git objects, never local INI/history/private files. Restore empty DU dirs."""
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    data = subprocess.check_output(['git', 'archive', revision, 'cpmcard'], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive:
            path = PurePosixPath(member.name)
            if path.parts[0] != 'cpmcard' or '..' in path.parts or path.is_absolute():
                raise ValueError('Unexpected card archive path')
            if any(p in ('private', '.7xtgv-desktop-lock') for p in path.parts):
                continue
            target = destination.joinpath(*path.parts[1:])
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(member).read())
            else:
                raise ValueError(f'Unsupported card archive entry: {member.name}')
    for drive in 'ABCDEFGHIJKLMNOP':
        for user in '0123456789ABCDEF':
            (destination / drive / user).mkdir(parents=True, exist_ok=True)
    return revision


def build_pages():
    BUILD.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pages-package-', dir=BUILD) as temporary:
        temporary = Path(temporary)
        revision = committed_card(temporary / 'card')
        site = temporary / 'site'
        package(site, card_root=temporary / 'card')
        html = (site / 'index.html').read_text()
        original = '<script src="xtgv_web.js"></script><script type="module" src="standalone.js"></script>'
        if html.count(original) != 1:
            raise ValueError('Standalone startup markup changed; review Pages bootstrap')
        (site / 'index.html').write_text(html.replace(original, '<script type="module" src="pages-start.js"></script>'))
        for name in ('pages-start.js', 'pages-worker.js'):
            shutil.copyfile(HERE / 'web' / name, site / name)
        (site / '.nojekyll').touch()
        for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
            shutil.copyfile(ROOT / name, site / name)
        shutil.copytree(ROOT / 'LICENSES', site / 'LICENSES')
        files = {p.relative_to(site).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in sorted(site.rglob('*')) if p.is_file()}
        (site / 'package.json').write_text(json.dumps({
            'card_commit': revision,
            'note': 'Card from Git HEAD. Runtime and frontend from the current local build/sources; CI rebuilds its checkout.',
            'sha256': files,
        }, indent=2) + '\n')
        # Only replace this tool's fixed, generated output directory.
        if OUTPUT.is_symlink():
            raise ValueError('Pages output must not be a symlink')
        if OUTPUT.exists():
            if not (OUTPUT / 'package.json').is_file():
                raise ValueError('Existing pages directory is not a generated package')
            shutil.rmtree(OUTPUT)
        shutil.move(str(site), OUTPUT)
    print('Pages package: build/RunCPM_7xtgv-terminal/desktop-web/pages/')
    print(f'Preloaded card: committed cpmcard at {revision[:12]} (all A–P/0–F directories).')
    print('No deployment performed. Preview with tools-web/pages-preview.sh.')


class PreviewHandler(SimpleHTTPRequestHandler):
    """Deliberately omit isolation headers, like GitHub Pages."""
    def list_directory(self, path):
        self.send_error(403)
        return None

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('package', 'preview'))
    parser.add_argument('--no-open', action='store_true')
    args = parser.parse_args()
    if args.action == 'package':
        build_pages()
        return
    if not (OUTPUT / 'package.json').is_file():
        parser.error('Run pages-package.sh first')
    # Serve an immutable copy under a subpath; rebuilding cannot affect a live tab.
    with tempfile.TemporaryDirectory(prefix='pages-preview-', dir=BUILD) as temp:
        shutil.copytree(OUTPUT, Path(temp) / 'preview')
        handler = functools.partial(PreviewHandler, directory=temp)
        with ThreadingHTTPServer(('127.0.0.1', 0), handler) as server:
            url = f'http://127.0.0.1:{server.server_port}/preview/?scale=2&target=ES3_35'
            print('*** WEB · Pages preview ***\nURL: ' + url, flush=True)
            print('First visit reloads once before boot. Download your card before closing.', flush=True)
            if not args.no_open:
                webbrowser.open(url)
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                pass


if __name__ == '__main__':
    main()
