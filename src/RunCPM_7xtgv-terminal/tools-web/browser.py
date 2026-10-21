#!/usr/bin/env python3
"""Serve a standalone static bundle with worker isolation headers. No guest bridge."""
import argparse
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
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


def lan_address():
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(('192.0.2.1', 9))
            candidate = probe.getsockname()[0]
            if not candidate.startswith('127.'):
                return candidate
    except OSError:
        pass
    return None


def lan_certificate(address):
    """Create a private local CA and an IP-specific server certificate."""
    state_root = Path(os.environ.get('XDG_STATE_HOME', Path.home() / '.local/state'))
    cert_dir = state_root / '7xtgv-web' / 'lan-certificate'
    cert_dir.mkdir(parents=True, exist_ok=True)
    cert_dir.chmod(0o700)
    ca_key, ca_cert = cert_dir / 'ca.key', cert_dir / 'ca.crt'
    if not (ca_key.is_file() and ca_cert.is_file()):
        subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:3072',
                        '-out', str(ca_key)], check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        ca_key.chmod(0o600)
        subprocess.run(['openssl', 'req', '-x509', '-new', '-key', str(ca_key), '-sha256',
                        '-days', '3650', '-out', str(ca_cert), '-subj', '/CN=7xtgv local web CA',
                        '-addext', 'basicConstraints=critical,CA:TRUE,pathlen:0',
                        '-addext', 'keyUsage=critical,keyCertSign,cRLSign'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    host_key, host_csr = cert_dir / 'server.key', cert_dir / 'server.csr'
    host_cert, extensions = cert_dir / 'server.crt', cert_dir / 'server.ext'
    subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:2048',
                    '-out', str(host_key)], check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    host_key.chmod(0o600)
    subprocess.run(['openssl', 'req', '-new', '-key', str(host_key), '-out', str(host_csr),
                    '-subj', f'/CN={address}'], check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    extensions.write_text('basicConstraints=critical,CA:FALSE\n'
                          'keyUsage=critical,digitalSignature,keyEncipherment\n'
                          'extendedKeyUsage=serverAuth\n'
                          f'subjectAltName=IP:{address}\n')
    subprocess.run(['openssl', 'x509', '-req', '-in', str(host_csr), '-CA', str(ca_cert),
                    '-CAkey', str(ca_key), '-CAcreateserial', '-out', str(host_cert),
                    '-days', '365', '-sha256', '-extfile', str(extensions)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return host_cert, host_key, ca_cert


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--window-scale', type=int, choices=range(1, 10), default=2)
    p.add_argument('--target', default='jc8048w535', help='Display ID or board target; also selectable in the page')
    p.add_argument('--lan', action='store_true', help='also serve the page on the local network')
    p.add_argument('--https', action='store_true', help='serve over HTTPS (required for LAN browser workers)')
    p.add_argument('--port', type=int, default=0, help='TCP port (default: choose a free port)')
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
    address = lan_address() if a.lan else None
    if a.https:
        if not address:
            p.error('--https requires a detected LAN address; use --lan')
        try:
            cert, key, ca_cert = lan_certificate(address)
        except (OSError, subprocess.CalledProcessError) as error:
            p.error(f'could not create the local HTTPS certificate (install OpenSSL): {error}')
        shutil.copyfile(ca_cert, site / 'lan-ca.crt')
    handler = functools.partial(StaticHandler, directory=str(site))
    bind_host = '0.0.0.0' if a.lan else '127.0.0.1'
    with ThreadingHTTPServer((bind_host, a.port), handler) as server:
        if a.https:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(cert, key)
            server.socket = context.wrap_socket(server.socket, server_side=True)
        scheme = 'https' if a.https else 'http'
        url_host = address if a.https else '127.0.0.1'
        url = f'{scheme}://{url_host}:{server.server_port}/?' + urlencode({'scale': a.window_scale, 'target': a.target})
        (session / 'session.json').write_text(json.dumps({'url':url, 'session':str(session)}, indent=2))
        print(f'*** WEB · {a.target} · standalone ***', flush=True)
        print('Browser keyboard, private memory card, native C++ terminal.', flush=True)
        print('URL:', url, flush=True)
        if a.lan:
            if address:
                network_url = f'{scheme}://{address}:{server.server_port}/?' + urlencode(
                    {'scale': a.window_scale, 'target': a.target})
                print('LAN URL:', network_url, flush=True)
            else:
                print('LAN URL: address not detected; find it with `ip -4 addr`.', flush=True)
            print('LAN access is unauthenticated; stop hosting when finished.', flush=True)
            if a.https:
                print('Install this CA certificate on the phone once, then reopen the LAN URL:', ca_cert, flush=True)
                print('The certificate is also available at /lan-ca.crt on the LAN URL.', flush=True)
                fingerprint = subprocess.run(['openssl', 'x509', '-in', str(ca_cert), '-noout',
                                              '-fingerprint', '-sha256'], check=True,
                                             capture_output=True, text=True).stdout.strip()
                print('CA SHA-256 fingerprint:', fingerprint, flush=True)
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
