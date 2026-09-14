#!/usr/bin/env python3
"""Target-aware host action for the CP/M A0:REFLASH.COM event."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
TARGETS = Path(__file__).resolve().parent / "targets"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default=os.environ.get("XTGV_TARGET"),
                        help="named target; defaults to XTGV_TARGET")
    parser.add_argument("--port", default=os.environ.get("XTGV_PORT"),
                        help="serial port; defaults to XTGV_PORT then target definition")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not args.target:
        parser.error("set XTGV_TARGET or pass --target")
    definition_path = TARGETS / f"{args.target}.json"
    try:
        definition = json.loads(definition_path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        parser.error(f"unknown target {args.target!r}: no {definition_path.name}")
    if definition.get("method") != "arduino-cli":
        parser.error(f"{args.target} requires {definition.get('method', 'an unspecified')} upload workflow")
    port = args.port or definition.get("port")
    if not port:
        parser.error(f"{args.target} has no default port; set XTGV_PORT")
    cli = os.environ.get("ARDUINO_CLI", "/home/falken/.local/bin/arduino-cli")
    # Arduino CLI accepts build properties only for `compile`, not `upload`.
    # Compile to a named target artifact first, then upload exactly that output;
    # this also prevents an event for one panel from reusing another panel's
    # cached firmware.
    build_dir = ROOT.parent.parent / "build" / f"build-{args.target}"
    compile_command = [cli, "compile", "--profile", definition["profile"],
                       "--output-dir", str(build_dir)]
    for key, value in definition.get("build_properties", {}).items():
        compile_command.extend(["--build-property", f"{key}={value}"])
    compile_command.append(str(ROOT))
    upload_command = [cli, "upload", "--profile", definition["profile"],
                      "--port", port, "--input-dir", str(build_dir), str(ROOT)]
    print("7xtgv reflash build:", " ".join(compile_command), flush=True)
    print("7xtgv reflash upload:", " ".join(upload_command), flush=True)
    if args.dry_run:
        return 0
    if subprocess.run(compile_command, check=False).returncode:
        return 1
    return subprocess.run(upload_command, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
