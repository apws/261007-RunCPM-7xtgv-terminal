#!/usr/bin/env python3
"""Target-aware host action for the CP/M A0:REFLASH.COM event."""
import argparse
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys
from firmware_identity import require_target, IdentityUnavailable, TargetMismatch
from target_config import (load_target, resolve_port, arduino_cli, target_label,
                           arduino_command, ensure_serial_access)


ROOT = Path(__file__).resolve().parent.parent
TARGETS = Path(__file__).resolve().parent / "targets"


def resolved_build_properties(cli, definition, stamp, overrides=None):
    """Append project flags to the selected core/profile's expanded defaults.

    Replacing build.extra_flags outright loses USB, PSRAM and core-affinity
    definitions. Ask Arduino before overriding that one property; keep its
    quoting intact and never infer a board's flags from the target name.
    """
    properties = dict(definition.get("build_properties", {}))
    if overrides:
        properties.update(overrides)
    additions = properties.pop("build.extra_flags", "")
    command = arduino_command(cli, "compile", "--profile", definition["profile"],
                              "--show-properties=expanded")
    for key, value in properties.items():
        command.extend(["--build-property", f"{key}={value}"])
    command.append(str(ROOT))
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError("Cannot resolve Arduino build flags: " + result.stderr.strip())
    defaults = [line.partition("=")[2] for line in result.stdout.splitlines()
                if line.startswith("build.extra_flags=")]
    if len(defaults) != 1 or "{" in defaults[0] or "}" in defaults[0]:
        raise RuntimeError("Arduino did not return one fully expanded build.extra_flags value")
    properties["build.extra_flags"] = " ".join(
        part.strip() for part in (defaults[0], additions, f"-DXTGV_BUILD_STAMP={stamp}")
        if part.strip())
    return properties


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
    parser.add_argument("--optimization", choices=("Os", "O2", "O3"),
                        help="RP2350 or P4 trial build: override whole source-build optimization")
    parser.add_argument("--w535-sd-mmc", action="store_true",
                        help="W535 fallback build: select the former one-bit SD_MMC adapter instead of default SPI3/SdFat")
    parser.add_argument("--storage-read-only", action="store_true",
                        help="P4 test build: reject filesystem sector writes, independent of INI")
    parser.add_argument("--promote-new", action="store_true",
                        help="accept the tested new candidate as current, then upload it")
    parser.add_argument("--current", action="store_true",
                        help="upload the hardware-proven current artifact, bypassing new")
    parser.add_argument("--restore", metavar="ARCHIVE",
                        help="promote a named target archive to current before upload")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--allow-unidentified-target", metavar="TARGET",
                        help="explicit manual recovery for old/nonresponding firmware; never overrides a mismatch")
    args = parser.parse_args()
    if sum(bool(option) for option in (args.build, args.build_only, args.promote_new,
                                       args.current, args.restore)) > 1:
        parser.error("--build, --build-only, --promote-new, --current, and --restore are mutually exclusive")
    building = args.build or args.build_only
    if args.storage_read_only and (not building or args.target != "jc8012p4a1c"):
        parser.error("--storage-read-only requires --target jc8012p4a1c and --build or --build-only")
    if args.optimization and not building:
        parser.error("--optimization requires --build or --build-only")
    if not args.target:
        parser.error("set XTGV_TARGET or pass --target")
    if args.allow_unidentified_target and args.allow_unidentified_target != args.target:
        parser.error("--allow-unidentified-target must repeat the selected --target exactly")
    try:
        _, definition = load_target(args.target, TARGETS)
    except RuntimeError as error:
        parser.error(str(error))
    if args.optimization and args.target not in (
            "rp2350-lcd-1-47-a", "rp2350-touch-lcd-2", "jc8012p4a1c"):
        parser.error("--optimization trials support RP2350 targets and jc8012p4a1c")
    if args.w535_sd_mmc and (args.target != "jc8048w535" or not building):
        parser.error("--w535-sd-mmc requires --target jc8048w535 and --build or --build-only")
    if definition.get("method") != "arduino-cli" and not (
            args.build_only and definition.get("method") == "bootsel-uf2"):
        parser.error(f"{args.target} requires {definition.get('method', 'an unspecified')} upload workflow")
    try:
        port = None if args.build_only else resolve_port(args.port, args.target, definition)
        cli = arduino_cli()
        if port and not args.dry_run and os.path.exists(port):
            ensure_serial_access(port)
    except RuntimeError as error:
        parser.error(str(error))
    print("7xtgv target:", target_label(args.target, definition), flush=True)
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
    compile_command = arduino_command(cli, "compile", "--profile", definition["profile"],
                                      "--output-dir", str(build_dir))
    build_properties = dict(definition.get("build_properties", {}))
    if building:
        try:
            optimization_key = ("compiler.optimization_flags" if args.target == "jc8012p4a1c"
                                else "build.flags.optimize")
            overrides = ({optimization_key: f"-{args.optimization}"}
                         if args.optimization else None)
            build_properties = resolved_build_properties(cli, definition, stamp, overrides)
            if args.w535_sd_mmc:
                build_properties["build.extra_flags"] += " -DXTGV_W535_SD_MMC=1"
            if args.storage_read_only:
                build_properties["build.extra_flags"] += (
                    " -DXTGV_STORAGE_READ_ONLY=1 -DUSE_BLOCK_DEVICE_INTERFACE=1")
        except RuntimeError as error:
            parser.error(str(error))
    for key, value in build_properties.items():
        compile_command.extend(["--build-property", f"{key}={value}"])
    compile_command.append(str(ROOT))
    # A build is uploaded from its eventual `new` location; promotion and
    # restore both upload their eventual `current` location.  This keeps a
    # dry-run truthful as well as the real post-rename invocation.
    upload_dir = candidate_dir if building else (
        current_dir if args.current or args.promote_new or args.restore or not candidate_dir.is_dir()
        else candidate_dir)
    upload_command = [] if args.build_only else arduino_command(
        cli, "upload", "--profile", definition["profile"],
        "--port", port, "--input-dir", str(upload_dir), str(ROOT))
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
    # Every upload path (including device-triggered REFLASH) checks before
    # promotion/restoration or invoking the uploader. Build-only is offline.
    def check_device():
        try:
            if os.path.exists(port):
                ensure_serial_access(port)
            require_target(port, args.target, args.allow_unidentified_target)
        except (RuntimeError, OSError) as error:
            print(f"7xtgv reflash blocked: {error}", file=sys.stderr)
            if isinstance(error, IdentityUnavailable):
                print("For intentional recovery/first installation only, repeat with "
                      f"--allow-unidentified-target {args.target}", file=sys.stderr)
            return 1
        return 0
    if not building and check_device():
        return 1
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
        if not args.build_only:
            upload_command[upload_command.index("--input-dir") + 1] = str(build_dir)
    elif not build_dir.is_dir():
        parser.error(f"no stable image for {args.target}: run host reflash with --build first")
    if args.build_only:
        return 0
    # Build+upload may take a while: query after compilation, not before it.
    if building and check_device():
        return 1
    return subprocess.run(upload_command, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
