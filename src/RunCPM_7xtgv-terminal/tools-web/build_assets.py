#!/usr/bin/env python3
"""Package standalone static assets. Build-time only; never writes the source card."""
import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
import sys
import zipfile
from urllib.parse import quote

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / 'build/RunCPM_7xtgv-terminal/desktop-web'
REPOSITORY_WEB = 'https://github.com/apws/261007-RunCPM-7xtgv-terminal/blob/main'
sys.path.insert(0, str(HERE.parent / 'tools'))
from key_event_ids import key_ids
from host_key_config import keyboard_environment


def package_guides(output):
    """Package only the numbered guides and their available printed PDFs."""
    destination = output / 'guides'
    destination.mkdir(exist_ok=True)
    rows = []
    for source in sorted((ROOT / 'guides').glob('[0-9][0-9]-*-guide.adoc')):
        number, abbreviation, slug = source.stem.split('-', 2)
        title = slug.replace('-', ' ').title()
        if abbreviation == 'TRG':
            title = 'Technical Reference Guide'
        shutil.copyfile(source, destination / source.name)
        pdfs = list((ROOT / 'guides/pdf').glob(f'* - {number} {abbreviation} *.pdf'))
        if len(pdfs) > 1:
            raise ValueError(f'Ambiguous printed guide: {source.name}')
        def document_link(path, kind):
            url = (f'{REPOSITORY_WEB}/guides/{quote(path.name)}' if kind == 'ADOC'
                   else f'guides/{quote(path.name)}')
            return (f'<a class="guide-format" href="{url}" '
                    f'target="_blank" rel="noopener" aria-label="{html.escape(title)} ({kind})" '
                    f'title="{html.escape(title)} · {kind}">{kind}</a>')
        pdf = '<span class="guide-format unavailable" title="PDF not available" aria-label="PDF not available">PDF</span>'
        if pdfs:
            shutil.copyfile(pdfs[0], destination / pdfs[0].name)
            pdf = document_link(pdfs[0], 'PDF')
        rows.append(f'<li>{pdf}{document_link(source, "ADOC")}<span>{html.escape(title)}</span></li>')
    if not rows:
        raise ValueError('No numbered guides found')
    return '\n'.join(rows)


def package_keycaps(output):
    """Keep the guide stylesheet as the single source for keycap appearance."""
    css = (ROOT / 'asciidoctorjs-crx-mixworx.css').read_text()
    variables = re.findall(r'  --mixworx-(?:text-font|keycap-[\w-]+):[^;]+;', css)
    start = css.index('.keyseq {')
    end = css.index('\n.menuseq,', start)
    (output / 'keycaps.css').write_text(
        '/* Generated from asciidoctorjs-crx-mixworx.css; edit that source. */\n'
        + ':root {\n' + '\n'.join(variables) + '\n}\n' + css[start:end])


def pack_card(root, output):
    total = count = 0
    if not (root / 'A/0').is_dir():
        raise ValueError('Card must contain A/0')
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('cpmcard/', b'')
        for folder, dirs, files in os.walk(root, followlinks=False):
            dirs[:] = sorted(d for d in dirs if d not in ('private', '.7xtgv-desktop-lock')
                             and not (Path(folder) / d).is_symlink())
            relative = Path(folder).relative_to(root)
            if relative.parts:
                archive.writestr('cpmcard/' + relative.as_posix() + '/', b'')
                count += 1
            for name in sorted(files):
                path = Path(folder) / name
                if path.is_symlink() or not path.is_file():
                    continue
                data = path.read_bytes()
                total += len(data)
                count += 1
                if total > 32 * 1024 * 1024 or count > 10000:
                    raise ValueError('Card exceeds 32 MiB / 10000 entries')
                archive.writestr('cpmcard/' + (relative / name).as_posix(), data)


def package(output, card_root=None, card_zip=None):
    output.mkdir(parents=True, exist_ok=True)
    vendor = HERE / 'web/vendor/fflate.js'
    if hashlib.sha256(vendor.read_bytes()).hexdigest() != '8cc1f687e0159e977addb6b85e274dbd11e622cf151f4fcb7b85d49622ea43e7':
        raise ValueError('Vendored fflate checksum mismatch')
    for name in ('input.js', 'serial_link.js', 'display.js', 'screen_text.js', 'card.js',
                 'standalone.js', 'standalone.css'):
        shutil.copyfile(HERE / 'web' / name, output / name)
    page = (HERE / 'web/standalone.html').read_text()
    marker = '<!-- guide-links -->'
    if page.count(marker) != 1:
        raise ValueError('Standalone guide list marker missing or duplicated')
    (output / 'index.html').write_text(page.replace(marker, package_guides(output)))
    package_keycaps(output)
    shutil.copytree(HERE / 'web/vendor', output / 'vendor', dirs_exist_ok=True)
    for path in (BUILD / 'shared').glob('xtgv_web.*'):
        if path.suffix in ('.js', '.wasm'):
            shutil.copyfile(path, output / path.name)
    if not (output / 'xtgv_web.wasm').is_file():
        raise ValueError('Run tools-web/desktop-rebuild.sh first')
    config = keyboard_environment()['XTGV_HACK_TOPROW']
    (output / 'protocol.js').write_text('// Generated from C++ key IDs and host config; edit their sources.\n'
        + 'export const keyIds = ' + json.dumps(key_ids(), indent=2) + ';\n'
        + 'export const defaultTopRow = ' + json.dumps(config, ensure_ascii=False) + ';\n')
    if card_zip:
        shutil.copyfile(card_zip, output / 'card.zip')
    else:
        pack_card(card_root or ROOT / 'cpmcard', output / 'card.zip')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=BUILD / 'site')
    p.add_argument('--card-root', type=Path)
    p.add_argument('--card-zip', type=Path)
    a = p.parse_args()
    package(a.output, a.card_root, a.card_zip)
    print('Standalone static bundle:', a.output)


if __name__ == '__main__':
    main()
