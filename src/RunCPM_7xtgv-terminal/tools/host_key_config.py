"""Shared host keyboard configuration for Linux launchers and Windows Mintty."""
from pathlib import Path
import os
import re
import shlex

TOPROW_SETTING = 'XTGV_HACK_TOPROW'


def keyboard_environment(config=None, environ=None):
    """Read one literal host setting; never execute the shell config."""
    result = dict(os.environ if environ is None else environ)
    if TOPROW_SETTING not in result:
        config = Path(config) if config is not None else Path(__file__).resolve().with_name('terminal.conf')
        value = ''
        if config.is_file():
            for line in config.read_text(encoding='utf-8').splitlines():
                match = re.fullmatch(r'\s*(?:export\s+)?XTGV_HACK_TOPROW\s*=(.*)', line)
                if match:
                    try:
                        words = shlex.split(match[1], comments=True)
                    except ValueError as error:
                        raise RuntimeError(f'{TOPROW_SETTING}: use a single quoted literal') from error
                    if len(words) > 1:
                        raise RuntimeError(f'{TOPROW_SETTING}: use a single quoted literal')
                    value = words[0] if words else ''
        result[TOPROW_SETTING] = value
    value = result[TOPROW_SETTING]
    if value and (len(value) != 10 or any(ord(c) < 32 or ord(c) > 0xffff or 0xd800 <= ord(c) <= 0xdfff for c in value)):
        raise RuntimeError(f'{TOPROW_SETTING}: expected ten BMP characters in positions 1..0, or empty to disable')
    return result
