#!/usr/bin/env python3
"""Incrementally print saved guide PDFs during web rebuilds, never during launch."""
import argparse
import asyncio
import hashlib
import html as html_text
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.parse import urljoin

from build_assets import BUILD, HERE, ROOT, REPOSITORY_WEB

SETTINGS = json.loads((HERE / 'guide-print-profile.json').read_text())
MANIFEST = 'print-manifest.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def guide_inputs(root):
    shared = hashlib.sha256(json.dumps(SETTINGS, sort_keys=True).encode())
    for path in [Path(__file__), HERE / 'requirements.txt', HERE / 'guide-print-profile.json',
                 root / 'asciidoctorjs-crx-mixworx.css', root / 'asciidoctorjs-crx-mixworx.js']:
        shared.update(path.name.encode() + b'\0' + path.read_bytes())
    # Assets are shared conservatively: changing any guide image invalidates
    # the snapshots, even if only one guide currently references that image.
    for path in sorted((root / 'guides/images').rglob('*')):
        if path.is_file() and not path.is_symlink() and 'private' not in path.parts:
            shared.update(str(path.relative_to(root)).encode() + b'\0' + path.read_bytes())
    jobs = []
    for source in sorted((root / 'guides').glob('[0-9][0-9]-*-guide.adoc')):
        number, abbreviation, slug = source.stem.split('-', 2)
        title = 'Technical Reference Guide' if abbreviation == 'TRG' else slug.replace('-', ' ').title()
        fingerprint = shared.copy()
        fingerprint.update(source.name.encode() + b'\0' + source.read_bytes())
        jobs.append(dict(source=source, name=f'RunCPM 7xtgv-terminal - {number} {abbreviation} {title}.pdf',
                         input_sha256=fingerprint.hexdigest()))
    if not jobs:
        raise RuntimeError('No numbered guides found')
    return jobs


def read_manifest(path):
    try:
        state = json.loads(path.read_text())
        return state if state.get('schema') == 1 else {}
    except (FileNotFoundError, ValueError):
        return {}


def prerequisites():
    missing = [name for name in ('asciidoctor', 'pdfinfo', 'pdftotext', 'fc-match') if not shutil.which(name)]
    if missing:
        raise RuntimeError('Missing PDF tools: ' + ', '.join(missing) + '; see tools-web/README.adoc setup')
    for family in ('Liberation Sans', 'Liberation Mono'):
        found = subprocess.check_output(['fc-match', '-f', '%{family}', family], text=True)
        if family not in found:
            raise RuntimeError('Missing print font: ' + family)


def validate_pdf(path):
    environment = dict(os.environ, LC_ALL='C')
    info = subprocess.check_output(['pdfinfo', str(path)], text=True, env=environment)
    size = re.search(r'Page size:\s*([\d.]+) x ([\d.]+) pts', info)
    pages = re.search(r'Pages:\s*(\d+)', info)
    width, height = {'Legal': (612, 1008), 'A4': (595.28, 841.89),
                     'Letter': (612, 792)}[SETTINGS['format']]
    if not size or abs(float(size[1]) - width) > 2 or abs(float(size[2]) - height) > 2:
        raise RuntimeError('PDF is not portrait ' + SETTINGS['format'] + ': ' + path.name)
    text = subprocess.check_output(['pdftotext', str(path), '-'], text=True, env=environment)
    if not pages or int(pages[1]) < 1 or len(text.strip()) < 100:
        raise RuntimeError('Empty or unreadable PDF: ' + path.name)
    urls = subprocess.check_output(['pdfinfo', '-url', str(path)], text=True, env=environment)
    if 'file://' in urls:
        raise RuntimeError('PDF contains a workstation-local link: ' + path.name)
    return int(pages[1])


async def render(jobs, stage, root):
    prerequisites()
    from playwright.async_api import async_playwright
    converter = subprocess.check_output(['asciidoctor', '--version'], text=True).splitlines()[0]
    for job in jobs:
        html = stage / (job['source'].stem + '.html')
        subprocess.run(['asciidoctor', '--failure-level', 'ERROR', '-s', '-a', 'showtitle',
                        '-a', 'webfonts!', '-a', 'source-highlighter!',
                        '-o', str(html), str(job['source'])], check=True)
        # Embedded CSS and local assets work offline. Relative PDF document
        # links are rewritten to the public repository before printing.
        # Do not set a <base>: Chromium would turn internal TOC destinations
        # into external file links in the PDF. Resolve image resources only.
        def image_url(match):
            url = urljoin((root / 'guides').as_uri() + '/', html_text.unescape(match[2]))
            return match[1] + html_text.escape(url, quote=True) + match[3]
        content = re.sub(r'(<img\b[^>]*\bsrc=")([^"]+)(")', image_url, html.read_text())
        # Live Preview renders an embedded document inside #content. Using the
        # standalone converter's #header changes the title and TOC print style.
        css = (root / 'asciidoctorjs-crx-mixworx.css').read_text()
        html.write_text('<!DOCTYPE html><html><head><meta charset="utf-8">'
                        '<meta name="generator" content="Asciidoctor">'
                        '<title>' + html_text.escape(job['source'].stem) + '</title>'
                        '<style>' + css + '</style></head><body class="article">'
                        '<div id="content">' + content + '</div></body></html>')
    async with async_playwright() as playwright:
        executable = os.environ.get('XTGV_PDF_BROWSER')
        if executable is None:
            executable = shutil.which('chromium') or shutil.which('google-chrome')
        browser = await playwright.chromium.launch(executable_path=executable or None, headless=True,
                                                   args=['--no-sandbox'])
        try:
            for job in jobs:
                page = await browser.new_page()
                await page.route(re.compile(r'^https?://'), lambda route: route.abort())
                await page.goto((stage / (job['source'].stem + '.html')).as_uri(), wait_until='load')
                await page.add_script_tag(content=(root / 'asciidoctorjs-crx-mixworx.js').read_text())
                await page.evaluate('document.fonts.ready')
                broken = await page.evaluate('''() => Array.from(document.images)
                    .filter(image => !image.complete || !image.naturalWidth).map(image => image.src)''')
                if broken:
                    raise RuntimeError('Missing guide images: ' + ', '.join(broken))
                await page.evaluate('''base => {
                    for (const a of document.querySelectorAll('a[href]')) {
                        const href = a.getAttribute('href');
                        if (!href.startsWith('#') && !/^[a-z]+:/i.test(href))
                            a.href = new URL(href, base).href;
                    }
                }''', REPOSITORY_WEB + '/guides/')
                await page.pdf(path=str(stage / job['name']), **SETTINGS)
                await page.close()
                print('Printed:', job['name'], flush=True)
            return dict(asciidoctor=converter, chromium=browser.version)
        finally:
            await browser.close()


async def update(root=ROOT, output=None, force=False, check=False):
    output = output or root / 'guides/pdf'
    jobs = guide_inputs(root)
    state = read_manifest(output / MANIFEST)
    previous = state.get('guides', {})
    pending = []
    before = {}
    for job in jobs:
        before[job['name']] = digest(output / job['name'])
        old = previous.get(job['name'], {})
        if force or not before[job['name']] or old.get('input_sha256') != job['input_sha256'] or \
                old.get('pdf_sha256') != before[job['name']]:
            pending.append(job)
    if not pending:
        print(f'Guide PDFs: all {len(jobs)} up to date (unchanged).')
        return 0
    if check:
        print('Guide PDFs need printing: ' + ', '.join(job['source'].stem for job in pending))
        return 1
    (BUILD / 'guide-pdf').mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='print-', dir=BUILD / 'guide-pdf') as folder:
        stage = Path(folder)
        renderer = await render(pending, stage, root)
        records = dict(previous)
        for job in pending:
            path = stage / job['name']
            records[job['name']] = dict(source=str(job['source'].relative_to(root)),
                input_sha256=job['input_sha256'], pdf_sha256=digest(path), pages=validate_pdf(path))
        # Do not replace reviewed files if their inputs or outputs changed while
        # the browser was printing. No saved PDF is touched on render failure.
        if guide_inputs(root) != jobs or any(digest(output / job['name']) != before[job['name']] for job in jobs):
            raise RuntimeError('Guides changed while printing; rerun the web rebuild')
        output.mkdir(parents=True, exist_ok=True)
        # Copy to the destination filesystem before atomic replacement; a
        # preview output directory may be on a different mount from build/.
        with tempfile.TemporaryDirectory(prefix='.print-', dir=output) as folder:
            ready = Path(folder)
            for job in pending:
                shutil.copyfile(stage / job['name'], ready / job['name'])
            manifest = ready / MANIFEST
            manifest.write_text(json.dumps(dict(schema=1, settings=SETTINGS, renderer=renderer,
                guides={job['name']: records[job['name']] for job in jobs}), indent=2, sort_keys=True) + '\n')
            for job in pending:
                (ready / job['name']).replace(output / job['name'])
            manifest.replace(output / MANIFEST)
    print(f'Guide PDFs: updated {len(pending)} of {len(jobs)}.')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, help='Preview destination; defaults to saved guides/pdf')
    parser.add_argument('--force', action='store_true', help='Reprint even when source and PDF hashes match')
    parser.add_argument('--check', action='store_true', help='Only report whether the saved PDFs are current')
    args = parser.parse_args()
    return asyncio.run(update(output=args.output_dir.resolve() if args.output_dir else None,
                              force=args.force, check=args.check))


if __name__ == '__main__':
    raise SystemExit(main())
