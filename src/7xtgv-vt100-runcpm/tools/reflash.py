#!/usr/bin/env python3
"""Target-aware host action for the CP/M A0:REFLASH.COM event."""
import argparse
from datetime import datetime
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
    parser.add_argument("--build", action="store_true",
                        help="build a target-aware candidate as new, then upload it for hardware test")
    parser.add_argument("--build-only", action="store_true",
                        help="build and stage a target-aware new candidate without uploading")
    parser.add_argument("--promote-new", action="store_true",
                        help="accept the tested new candidate as current, then upload it")
    parser.add_argument("--current", action="store_true",
                        help="upload the hardware-proven current artifact, bypassing new")
    parser.add_argument("--restore", metavar="ARCHIVE",
                        help="promote a named target archive to current before upload")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if sum(bool(option) for option in (args.build, args.build_only, args.promote_new,
                                       args.current, args.restore)) > 1:
        parser.error("--build, --build-only, --promote-new, --current, and --restore are mutually exclusive")
    building = args.build or args.build_only
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
    # A device-side REFLASH event must never compile whatever happens to be in
    # the working tree. It uploads the latest target-aware `new` candidate,
    # falling back to hardware-proven `current` only when no candidate exists.
    # Promotion is explicit. Keeping both beside timestamped archives makes
    # ordinary filename sorting a useful per-board timeline.
    build_root = ROOT.parent.parent / "build"
    target_root = build_root / ROOT.name / args.target
    current_dir = target_root / "current"
    candidate_dir = target_root / "new"
    build_dir = current_dir if args.current or not candidate_dir.is_dir() else candidate_dir
    staging_dir = None
    if building:
        stamp = datetime.now().strftime("%y%m%d-%H%M")
        staging_dir = target_root.parent / ".staging" / f"{stamp}-{args.target}-{os.getpid()}"
        staging_dir.parent.mkdir(parents=True, exist_ok=True)
        build_dir = staging_dir
    elif args.promote_new:
        build_dir = candidate_dir
    restore_dir = None
    if args.restore:
        restore_name = Path(args.restore).name
        if restore_name != args.restore or restore_name == "current":
            parser.error("--restore must name one archive directly under the selected target")
        restore_dir = target_root / restore_name
        if not restore_dir.is_dir():
            parser.error(f"no target archive named {restore_name!r}")
    if args.promote_new and not build_dir.is_dir():
        parser.error(f"no new candidate for {args.target}: run host reflash with --build first")
    compile_command = [cli, "compile", "--profile", definition["profile"],
                       "--output-dir", str(build_dir)]
    build_properties = dict(definition.get("build_properties", {}))
    if building:
        build_properties["build.extra_flags"] = (
            build_properties.get("build.extra_flags", "") +
            f" -DXTGV_BUILD_STAMP={stamp}").strip()
    for key, value in build_properties.items():
        compile_command.extend(["--build-property", f"{key}={value}"])
    compile_command.append(str(ROOT))
    # A build is uploaded from its eventual `new` location; promotion and
    # restore both upload their eventual `current` location.  This keeps a
    # dry-run truthful as well as the real post-rename invocation.
    upload_dir = candidate_dir if building else (
        current_dir if args.current or args.promote_new or args.restore or not candidate_dir.is_dir()
        else candidate_dir)
    upload_command = [cli, "upload", "--profile", definition["profile"],
                      "--port", port, "--input-dir", str(upload_dir), str(ROOT)]
    if building:
        print("7xtgv reflash build:", " ".join(compile_command), flush=True)
    if args.promote_new:
        print("7xtgv reflash promote:", build_dir, flush=True)
    if restore_dir:
        print("7xtgv reflash restore:", restore_dir, flush=True)
    if not args.build_only:
        print("7xtgv reflash upload:", " ".join(upload_command), flush=True)
    if args.dry_run:
        return 0
    if restore_dir:
        stamp = datetime.now().strftime("%y%m%d-%H%M")
        stable_dir = current_dir
        archive_dir = target_root / f"{stamp} before-restore"
        rejected_dir = target_root / f"{stamp} rejected-new"
        if archive_dir.exists():
            parser.error(f"refusing to overwrite existing archive {archive_dir}")
        if candidate_dir.exists() and rejected_dir.exists():
            parser.error(f"refusing to overwrite existing archive {rejected_dir}")
        if candidate_dir.exists():
            candidate_dir.rename(rejected_dir)
        if stable_dir.exists():
            stable_dir.rename(archive_dir)
        restore_dir.rename(stable_dir)
        build_dir = stable_dir
        upload_command[upload_command.index("--input-dir") + 1] = str(build_dir)
    elif args.promote_new:
        stamp = datetime.now().strftime("%y%m%d-%H%M")
        stable_dir = current_dir
        archive_dir = target_root / f"{stamp} before-promote"
        if archive_dir.exists():
            parser.error(f"refusing to overwrite existing archive {archive_dir}")
        if stable_dir.exists():
            stable_dir.rename(archive_dir)
        build_dir.rename(stable_dir)
        build_dir = stable_dir
        upload_command[upload_command.index("--input-dir") + 1] = str(build_dir)
    elif building:
        if subprocess.run(compile_command, check=False).returncode:
            return 1
        target_root.mkdir(parents=True, exist_ok=True)
        if candidate_dir.exists():
            archive_dir = target_root / f"{stamp} unverified"
            if archive_dir.exists():
                parser.error(f"refusing to overwrite existing archive {archive_dir}")
            candidate_dir.rename(archive_dir)
        staging_dir.rename(candidate_dir)
        build_dir = candidate_dir
        upload_command[upload_command.index("--input-dir") + 1] = str(build_dir)
    elif not build_dir.is_dir():
        parser.error(f"no stable image for {args.target}: run host reflash with --build first")
    return 0 if args.build_only else subprocess.run(upload_command, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
