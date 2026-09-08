#!/usr/bin/env python3
"""Disable the incompatible RGB-parallel path in the vendor P4 GFX fork."""

from pathlib import Path
import sys

FILES = (
    "src/databus/Arduino_ESP32RGBPanel.h",
    "src/databus/Arduino_ESP32RGBPanel.cpp",
    "src/display/Arduino_RGB_Display.h",
    "src/display/Arduino_RGB_Display.cpp",
)
OLD = "CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4"
NEW = "CONFIG_IDF_TARGET_ESP32S3"


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} ARDUINO_GFX_LIBRARY_DIR", file=sys.stderr)
        return 2

    root = Path(sys.argv[1]).expanduser()
    changed = 0
    for relative in FILES:
        path = root / relative
        text = path.read_text()
        if OLD in text:
            path.write_text(text.replace(OLD, NEW))
            changed += 1
            print(f"patched {path}")
        elif NEW in text:
            print(f"already compatible {path}")
        else:
            raise RuntimeError(f"unrecognized guard in {path}")

    print(f"done: {changed} file(s) changed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
