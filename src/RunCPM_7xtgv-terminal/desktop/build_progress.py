"""Stream full build output to its log, showing only Make/Ninja progress."""
import os
import re
import subprocess
import sys


def build_percent(line):
    line = re.sub(r'\x1b\[[0-9;]*m', '', line)
    match = re.match(r'^\[\s*(\d+)%\]', line)
    if match:
        return min(100, int(match[1]))
    match = re.match(r'^\[(\d+)/(\d+)\]', line)
    if match and int(match[2]):
        return min(100, int(match[1]) * 100 // int(match[2]))
    return None


def run_build(command, log, env=None):
    environment = dict(os.environ if env is None else env)
    environment['NINJA_STATUS'] = '[%f/%t] '
    terminal = sys.stdout.isatty()
    last = -1

    def show(percent, final=False):
        nonlocal last
        # Parallel builds can repeat/out-of-order their status; do not go back.
        if percent <= last or (not terminal and not final and percent < last + 5):
            return
        last = percent
        print(('\r' if terminal else '') + f'Building desktop: {percent:3d}%',
              end='' if terminal else '\n', flush=True)

    child = subprocess.Popen(command, env=environment, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True,
                             encoding='utf-8', errors='replace', bufsize=1)
    try:
        show(0, final=True)
        for line in child.stdout:
            log.write(line); log.flush()
            percent = build_percent(line)
            if percent is not None:
                # Only a successful process exit establishes build completion.
                show(min(99, percent))
        code = child.wait()
        if code:
            raise subprocess.CalledProcessError(code, command)
        show(100, final=True)
    except BaseException:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill(); child.wait()
        raise
    finally:
        child.stdout.close()
        if terminal:
            print(flush=True)
