#!/usr/bin/env python3
"""Linux launcher for the experimental browser/native-terminal proof."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import webbrowser

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / 'build/RunCPM_7xtgv-terminal/desktop-web'
sys.path.insert(0, str(HERE.parent / 'tools'))
from host_key_config import keyboard_environment
from generate_key_events import launch_config
from target_mirror import select_mirror


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--window-scale', type=int, choices=range(1, 10), default=2)
    parser.add_argument('--card-zip', type=Path, help='Import an exported card ZIP instead of the repository snapshot')
    parser.add_argument('--mirror', choices=('auto', 'konsole', 'kitty'), default='auto')
    args = parser.parse_args()
    if not (BUILD / 'shared/xtgv_web.wasm').is_file():
        parser.error('Run tools-web/setup.sh and desktop-rebuild.sh first.')
    mirror = select_mirror(args.mirror)
    if mirror == 'terminal' or not shutil.which(mirror):
        parser.error('This proof launcher needs Konsole or Kitty.')
    (BUILD / 'sessions').mkdir(parents=True, exist_ok=True)
    session = Path(tempfile.mkdtemp(prefix='local-', dir=BUILD / 'sessions'))
    print('*** WEB PROOF · ES3_35 ***', flush=True)
    print('Private in-memory card; native terminal input; passive browser display.', flush=True)
    print('Session:', session, flush=True)
    with (session / 'server.log').open('wb') as log:
        command = [sys.executable, str(HERE / 'server.py'), '--session-dir', str(session),
                   '--window-scale', str(args.window_scale)]
        if args.card_zip:
            command += ['--card-zip', str(args.card_zip.resolve())]
        server = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            for _ in range(100):
                if (session / 'session.json').exists():
                    break
                if server.poll() is not None:
                    raise RuntimeError('Bridge startup failed; see session/server.log')
                time.sleep(.1)
            info = json.loads((session / 'session.json').read_text())
            url = info['url'] + '?scale=' + str(args.window_scale)
            webbrowser.open(url)
            print('Waiting for the browser…', flush=True)
            for _ in range(1200):
                if server.poll() is not None:
                    return
                with urllib.request.urlopen(info['url'] + 'status', timeout=2) as response:
                    if json.load(response)['status'] == 'running':
                        break
                time.sleep(.1)
            else:
                raise RuntimeError('Browser did not start within two minutes.')
            env = keyboard_environment()
            child = [sys.executable, str(HERE / 'mirror.py'), str(session)]
            if mirror == 'konsole':
                env['XDG_DATA_HOME'] = str(session / 'xdg')
                folder = session / 'xdg/konsole'
                folder.mkdir(parents=True)
                shutil.copyfile(HERE.parent / 'tools/7xtgv-terminal.profile', folder / '7xtgv-terminal.profile')
                keymap = launch_config('konsole', folder, env['XTGV_HACK_TOPROW'])
                command = ['konsole', '--separate', '--profile', '7xtgv-terminal', '-p', 'KeyBindings=' + keymap,
                           '-p', 'LocalTabTitleFormat=7xtgv web proof / ES3_35', '-e'] + child
            else:
                config = launch_config('kitty', session / 'keys', env['XTGV_HACK_TOPROW'])
                command = ['kitty', '--config', config] + child
            with (session / 'terminal.log').open('wb') as terminal_log:
                terminal = subprocess.Popen(command, env=env, stdout=terminal_log, stderr=subprocess.STDOUT)
                print('Running. Exit the mirror with Ctrl+] then Q; download the card from the browser.', flush=True)
                print('Close the browser page when finished to release the local bridge.', flush=True)
                server.wait()
                terminal.wait(timeout=10)
        except KeyboardInterrupt:
            pass
        finally:
            if server.poll() is None:
                server.terminate()
                server.wait(timeout=15)


if __name__ == '__main__':
    main()
