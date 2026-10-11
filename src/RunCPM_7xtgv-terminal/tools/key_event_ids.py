"""Read host key identifiers from the shared firmware definition."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def key_ids():
    header = (ROOT / "src/terminal/key_chords.h").read_text()
    body = re.search(r"enum KeyBase\s*:\s*uint8_t\s*\{(.*?)\};", header, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    ids = {}
    current = -1
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        if "=" in item:
            name, number = item.split("=", 1)
            current = int(number.strip())
            name = name.strip()
        else:
            current += 1
            name = item
        ids[name] = current
    return ids

