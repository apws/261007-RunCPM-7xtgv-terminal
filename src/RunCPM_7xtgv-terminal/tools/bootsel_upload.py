"""Conservative Linux RP2350 UF2 upload, without firmware promotion."""
import glob
import os
from pathlib import Path
import re
import struct
import subprocess
import sys


class NoBootselDisk(RuntimeError):
    pass


def validate_uf2(image, target):
    raw = Path(image).read_bytes()
    if not raw or len(raw) % 512:
        raise RuntimeError('Invalid UF2 length')
    payload = bytearray()
    has_arm = False
    for offset in range(0, len(raw), 512):
        block = raw[offset:offset + 512]
        magic0, magic1, flags, address, size, number, count, family = struct.unpack(
            '<8I', block[:32])
        if (magic0, magic1, struct.unpack('<I', block[508:])[0]) != (
                0x0a324655, 0x9e5d5157, 0x0ab16f30) or size > 476:
            raise RuntimeError('Invalid UF2 block')
        if not flags & 0x2000 or family not in (0xe48bff57, 0xe48bff59):
            raise RuntimeError('Expected RP2350 absolute/ARM secure UF2 family')
        has_arm |= family == 0xe48bff59
        payload.extend(block[32:32 + size])
    if not has_arm or target.encode('ascii') + b'\0' not in payload:
        raise RuntimeError('UF2 does not contain the selected RP2350 target identity')


def choose_disk(expected_chip=None, paths=None):
    disks = []
    for path in paths if paths is not None else glob.glob(
            '/dev/disk/by-id/usb-RPI_RP2350_*-part1'):
        match = re.fullmatch(r'usb-RPI_RP2350_([A-Fa-f0-9]{16})-.*-part1',
                             Path(path).name)
        if match:
            disks.append((path, match[1].upper()))
    if expected_chip:
        if not disks:
            raise NoBootselDisk('No RP2350 BOOTSEL disk connected')
        disks = [item for item in disks if item[1] == expected_chip.upper()]
    elif not disks:
        raise NoBootselDisk('No RP2350 BOOTSEL disk connected')
    if len(disks) != 1:
        raise RuntimeError('Need exactly one matching RP2350 BOOTSEL disk. '
                           'Enter BOOTSEL manually; use --chip-id with multiple boards.')
    return disks[0]


def mounted_path(device, log=None):
    found = subprocess.run(['findmnt', '-n', '-o', 'TARGET', '--source', device],
                           text=True, capture_output=True, check=False)
    if found.returncode:
        subprocess.run(['udisksctl', 'mount', '-b', device], check=True,
                       stdout=log, stderr=subprocess.STDOUT if log else None)
        found = subprocess.run(['findmnt', '-n', '-o', 'TARGET', '--source', device],
                               text=True, capture_output=True, check=True)
    mount = Path(found.stdout.strip())
    if not mount.is_absolute() or not mount.is_dir():
        raise RuntimeError('Cannot resolve the BOOTSEL mount')
    return mount


def confirm_connection(module, bootsel=False):
    if not sys.stdin.isatty():
        raise RuntimeError('Missing device requires interactive connection confirmation')
    mode = ' in BOOTSEL mode' if bootsel else ''
    message = f'Connect the correct {module} module{mode} before flashing.'
    if sys.stdout.isatty() and not os.environ.get('NO_COLOR'):
        message = '\033[31m' + message + '\033[0m'
    print(message, flush=True)
    if input('Press Enter when ready (Ctrl+C cancels): ').strip():
        raise RuntimeError('Connection confirmation cancelled')


def confirm_bootsel(module):
    confirm_connection(module, bootsel=True)


def ready_disk(module, chip=None):
    while True:
        try:
            return choose_disk(chip)
        except NoBootselDisk:
            confirm_bootsel(module)


def upload(target, image, chip=None, log=None, module=None, progress=None):
    validate_uf2(image, target)
    device, actual_chip = ready_disk(module or target, chip)
    mount = mounted_path(os.path.realpath(device), log)
    info = (mount / 'INFO_UF2.TXT').read_text()
    if 'RP2350' not in info:
        raise RuntimeError('BOOTSEL volume does not identify RP2350')
    if log:
        log.write(f'Selected BOOTSEL device for: {module or target}\n'
                  f'BOOTSEL chip: {actual_chip}; mount: {mount}\n')
    # Recheck identity immediately before writing, including explicit chip case.
    if choose_disk(actual_chip)[0] != device:
        raise RuntimeError('BOOTSEL disk changed')
    if log:
        log.write(f'Uploading {image} to {actual_chip}\n')
        log.flush()
    if progress:
        progress('Uploading RP2350 firmware ...')
    with Path(image).open('rb') as source, (mount / Path(image).name).open('wb') as dest:
        while chunk := source.read(65536):
            dest.write(chunk)
        dest.flush()
        os.fsync(dest.fileno())
    return actual_chip
