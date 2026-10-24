#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Full-image install / recovery for the Noble NB4 over STM32 ROM DFU.

Unlike the normal ApexTX updater (which writes only the application and keeps
the installed bootloader), this writes the complete 2 MiB image, including the
bootloader, to internal flash at 0x08000000 and verifies it by read-back. It is
used for the first installation (from FlySky firmware) and for recovery, and it
drives the bundled `dfu-util` exactly as `tools/nb4-flash.sh` does.
"""
import datetime
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

USB_ID = '0483:df11'
BOOT_ADDRESS = 0x08000000
FLASH_SIZE = 0x200000
BOOT_SIZE = 0x20000
ROM_START_MAGIC = b'APXROM1\x00'


class RomFlashError(RuntimeError):
    pass


def _bundle_dir():
    # PyInstaller unpacks data/binaries into _MEIPASS at runtime.
    return Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))


def dfu_util_path():
    for base in (_bundle_dir() / 'dfu', _bundle_dir(), Path(__file__).resolve().parent / 'dfu'):
        exe = base / ('dfu-util.exe' if sys.platform == 'win32' else 'dfu-util')
        if exe.is_file():
            return str(exe)
    if not getattr(sys, 'frozen', False):
        system_dfu = shutil.which('dfu-util')
        if system_dfu:
            return system_dfu
    raise RomFlashError('dfu-util was not found in the app bundle or on PATH.')


# Hide the console window dfu-util would otherwise flash up on Windows.
_NO_WINDOW = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0


def _run(args, **kw):
    kw.setdefault('timeout', 15)
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, creationflags=_NO_WINDOW, **kw)


def validate_full_image(image):
    """Validate a complete, manifested ApexTX image and return its radio model."""
    if len(image) != FLASH_SIZE:
        raise RomFlashError('Select an exact 2 MiB ApexTX firmware.bin. Partial images and '
                            'application-only files cannot be installed through ROM DFU.')

    def ram(value):
        return 0x10000000 < value <= 0x10010000 or 0x20000000 < value <= 0x20030000

    def code(value, lower, upper):
        return lower <= value < upper and value & 1

    boot_stack, boot_reset = struct.unpack_from('<II', image, 0)
    errors = []
    if not ram(boot_stack):
        errors.append('bootloader stack pointer')
    if not code(boot_reset, 0x08000000, 0x08020000):
        errors.append('bootloader reset vector')
    if errors:
        raise RomFlashError('This is not a complete ApexTX image for the Noble NB4 family '
                            '(invalid ' + ', '.join(errors) + ').')

    # The normal updater owns the authoritative application checks: exact image
    # size, manifest fields, application CRC, vectors, target markers and
    # agreement between the marker and the manifest.
    try:
        from nb4_update_image import (VARIANTS, package_target, update_package,
                                      validate_full_image_auth)
        validate_full_image_auth(image)
        return VARIANTS[package_target(update_package(image))]
    except (KeyError, ValueError) as error:
        raise RomFlashError(str(error)) from error


def image_variant(image):
    """Return 'NB4' / 'NB4+' only for a fully validated ApexTX image."""
    try:
        return validate_full_image(image)
    except RomFlashError:
        return None


def inspect_flash(dump):
    """Identify what is currently installed from a full internal-flash read-back.

    ROM DFU reveals nothing by itself, but the radio's current image does:
    ApexTX images carry 'apextx-nb4[plus]-' / 'APXHW:' markers that name both the
    firmware and the radio variant. Stock FlySky firmware carries neither, so it
    is reported as 'other' and the model cannot be derived from it.
    Returns {'firmware': 'ApexTX'|'other', 'variant': 'NB4'|'NB4+'|None}.
    """
    app = dump[BOOT_SIZE:] if len(dump) > BOOT_SIZE else dump
    marks = set(m.decode() for m in re.findall(rb'apextx-(nb4[a-z0-9]*)-[0-9]', app))
    marks |= set(m.decode() for m in re.findall(rb'APXHW:([a-z0-9]+)\x00', app))
    if marks & {'nb4', 'nb4plus'}:
        return {'firmware': 'ApexTX', 'variant': 'NB4+' if 'nb4plus' in marks else 'NB4'}
    return {'firmware': 'other', 'variant': None}


def _dfuse_capacity(name):
    """Return bytes described by a DfuSe memory name, or None if malformed."""
    descriptor = re.fullmatch(r'@Internal Flash\s*/0x08000000/(.+)', name)
    if not descriptor:
        return None
    total = 0
    for segment in descriptor.group(1).split(','):
        match = re.fullmatch(r'(\d+)\*(\d+)([BbKkMm])([a-gA-G])', segment.strip())
        if not match:
            return None
        count, size, unit, _permissions = match.groups()
        scale = {'b': 1, 'k': 1024, 'm': 1024 * 1024}[unit.lower()]
        total += int(count) * int(size) * scale
    return total


def discover_rom_dfu(expected_serial=None):
    """Return the serial of one F429-sized ROM DFU device, or None.

    0483:df11 is shared by many STM32 products.  Accept it only when alternate
    zero describes exactly 2 MiB of internal flash at 0x08000000.
    """
    try:
        result = _run([dfu_util_path(), '-l'])
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RomFlashError(f'Could not inspect STM32 ROM DFU: {error}') from error
    listing = result.stdout
    if re.search(r'cannot open.*0483:df11', listing, re.IGNORECASE):
        if sys.platform == 'win32':
            raise RomFlashError('Windows can see STM32 ROM DFU but cannot open it. Install a '
                                'WinUSB-compatible driver for the STM32 BOOTLOADER device, then retry.')
        raise RomFlashError('STM32 ROM DFU was found but could not be opened. Close other '
                            'software using the radio, reconnect USB, and retry.')

    saw_stm32 = False
    serials = set()
    for line in listing.splitlines():
        if f'[{USB_ID}]' not in line:
            continue
        saw_stm32 = True
        if not re.search(r'\balt=0\b', line):
            continue
        name_match = re.search(r'name="([^"]+)"', line)
        serial_match = re.search(r'serial="([^"]+)"', line)
        if (name_match and serial_match and
                _dfuse_capacity(name_match.group(1)) == FLASH_SIZE):
            serials.add(serial_match.group(1))

    if expected_serial is not None:
        return expected_serial if expected_serial in serials else None
    if len(serials) > 1:
        raise RomFlashError('More than one compatible STM32 ROM DFU device is connected. '
                            'Connect only the radio you want to flash.')
    if saw_stm32 and not serials:
        raise RomFlashError('An STM32 ROM DFU device was found, but its internal-flash '
                            'descriptor is not the 2 MiB layout expected for the Noble. '
                            'Nothing was written.')
    return next(iter(serials)) if serials else None


def _device_args(serial):
    args = ['-d', USB_ID]
    if serial:
        args += ['-S', serial]
    return args


def backup_internal(out_path, progress, serial=None):
    """Save the radio's current 2 MiB internal flash (its existing firmware) to
    a file over ROM DFU for expert/manual restoration. The app intentionally
    installs only complete-image-checksummed ApexTX builds. ROM DFU cannot reach
    the external 8 MiB SPI NOR, so this is a firmware backup, not a full factory
    backup.
    """
    exe = dfu_util_path()
    serial = discover_rom_dfu(serial)
    if serial is None:
        raise RomFlashError('No radio in STM32 ROM DFU mode was found. Put the radio into '
                            'ROM DFU (hold the recessed grip button while switching it on) '
                            'and connect USB.')
    out_path = Path(out_path)
    partial = out_path.with_suffix(out_path.suffix + '.partial')
    if out_path.exists() or partial.exists():
        raise RomFlashError(f'Refusing to overwrite an existing backup: {out_path}')
    progress('Reading the current firmware from the radio...', 1)
    rc = _stream_dfu([exe, *_device_args(serial), '-a', '0',
                      '-s', f'{hex(BOOT_ADDRESS)}:{FLASH_SIZE}', '-U', str(partial)],
                     progress, lo=1, hi=21, phase='Backing up')
    if rc != 0 or not partial.is_file() or partial.stat().st_size != FLASH_SIZE:
        try:
            partial.unlink()
        except OSError:
            pass
        raise RomFlashError('The backup could not be read. Nothing on the radio was changed.')
    partial.replace(out_path)
    progress(f'Backup saved ({FLASH_SIZE // 1024} KiB). Keep this file private.', 22)


def safe_serial(serial):
    """Make a USB serial safe to place in a local backup filename."""
    value = re.sub(r'[^A-Za-z0-9._-]+', '_', serial or '').strip('._-')[:64]
    return value or 'radio'


def write_backup_metadata(backup, variant, serial):
    """Record enough information to identify and verify a private raw backup."""
    backup = Path(backup)
    digest = hashlib.sha256(backup.read_bytes()).hexdigest()
    metadata = {
        'format': 'apextx-nb4-internal-flash-backup-v1',
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'radio_variant': variant,
        'usb_serial': serial,
        'size': backup.stat().st_size,
        'sha256': digest,
        'warning': 'Private unit-specific backup; does not include external SPI NOR.',
    }
    destination = backup.with_suffix(backup.suffix + '.json')
    temporary = destination.with_suffix(destination.suffix + '.tmp')
    temporary.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    temporary.replace(destination)
    return destination


def automatic_start_address(image):
    """Optional verified-image vector that requests a one-shot automatic boot.

    AN3156 Leave DFU loads SP and reset from address/address+4. Older images
    without the entry still use their normal boot vector and manual power-on.
    """
    boot = image[:BOOT_SIZE]
    offset = boot.find(ROM_START_MAGIC)
    if offset < 0:
        return None
    vector = offset + len(ROM_START_MAGIC)
    if (boot.find(ROM_START_MAGIC, offset + 1) >= 0 or vector % 4 or
            vector + 8 > len(boot)):
        raise RomFlashError('The automatic-start vector is invalid. Nothing was written.')
    stack, reset = struct.unpack_from('<II', boot, vector)
    boot_stack, boot_reset = struct.unpack_from('<II', boot)
    if (stack != boot_stack or reset == boot_reset or not reset & 1 or
            not BOOT_ADDRESS <= reset < BOOT_ADDRESS + BOOT_SIZE):
        raise RomFlashError('The automatic-start vector is invalid. Nothing was written.')
    return BOOT_ADDRESS + vector


def run_install(image, progress, serial=None):
    """Write, verify and launch a complete image. `progress(message, percent)`."""
    validate_full_image(image)
    start_address = automatic_start_address(image)
    exe = dfu_util_path()
    serial = discover_rom_dfu(serial)
    if serial is None:
        raise RomFlashError('No radio in STM32 ROM DFU mode was found. Put the radio into '
                            'ROM DFU (hold the recessed grip button while switching it on) '
                            'and connect USB.')
    with tempfile.TemporaryDirectory(prefix='apextx-install-') as directory:
        image_path = Path(directory) / 'nb4_install_image.bin'
        read_path = Path(directory) / 'nb4_install_readback.bin'
        image_path.write_bytes(image)

        # Re-identify the same unit immediately before the destructive step.
        if discover_rom_dfu(serial) != serial:
            raise RomFlashError('The selected radio left ROM DFU before writing. Nothing was written.')
        progress('Writing the complete image to the radio...', 25)
        wr = _stream_dfu([exe, *_device_args(serial), '-a', '0',
                          '-s', hex(BOOT_ADDRESS), '-D', str(image_path)],
                         progress, lo=25, hi=69, phase='Writing')
        if wr != 0:
            raise RomFlashError('Writing the image failed. The radio stays in DFU; '
                                'reconnect and try again with a known-good cable.')
        progress('Reading the image back to verify...', 70)
        rd = _stream_dfu([exe, *_device_args(serial), '-a', '0',
                          '-s', f'{hex(BOOT_ADDRESS)}:{FLASH_SIZE}', '-U', str(read_path)],
                         progress, lo=70, hi=94, phase='Verifying')
        if (rd != 0 or not read_path.is_file() or
                read_path.stat().st_size != FLASH_SIZE):
            raise RomFlashError('Reading the image back failed. The radio stays in DFU.')
        if read_path.read_bytes() != image:
            raise RomFlashError('The read-back does not match the image. Nothing was started; '
                                'the radio stays in DFU. Retry with a known-good image and cable.')
        progress('Verified. Finishing up...', 96)
        # Use the new image's explicit resume entry only after exact read-back.
        # A normal cold boot retains its power-button qualification. See ST
        # AN3156, Leave DFU: https://www.st.com/resource/en/application_note/cd00264379.pdf
        try:
            _run([exe, *_device_args(serial), '-a', '0',
                  '-s', f'{hex(start_address or BOOT_ADDRESS)}:leave'])
        except (OSError, subprocess.TimeoutExpired):
            # The image is already verified. A manual power cycle remains the
            # correct next step even when the optional leave request times out.
            pass
        still_connected = True
        for _ in range(8):
            time.sleep(1)
            try:
                still_connected = discover_rom_dfu(serial) == serial
            except RomFlashError:
                still_connected = True
            if not still_connected:
                break
        if start_address and not still_connected:
            done = ('Installation complete and verified. The radio is starting automatically. '
                    'Confirm that it has returned to the home screen.')
        else:
            done = ('Installation complete and verified. Switch the radio OFF (hold the power '
                    'button until it is fully off), then switch it ON again to start it.')
        progress(done, 100)


def _stream_dfu(args, progress, lo, hi, phase):
    """Run dfu-util, mapping its percentage output into [lo, hi]."""
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, creationflags=_NO_WINDOW, bufsize=1)
    last = -1
    buf = ''
    while True:
        chunk = proc.stdout.read(1)
        if not chunk:
            break
        buf += chunk
        if chunk in ('\r', '\n'):
            for m in re.finditer(r'(\d+)%', buf):
                pct = int(m.group(1))
                if not 0 <= pct <= 100:
                    continue
                lower, upper, label = lo, hi, phase
                if phase == 'Writing':
                    # dfu-util reports two separate 0..100% sequences for a
                    # download: erase, then write. Keep the overall bar moving
                    # forward and show which operation is actually running.
                    middle = lo + (hi - lo) // 2
                    if buf.lstrip().startswith('Erase'):
                        upper, label = middle, 'Erasing flash'
                    else:
                        lower = middle
                mapped = lower + (upper - lower) * pct // 100
                if mapped > last:
                    last = mapped
                    progress(f'{label} {pct}%', mapped)
            buf = ''
    proc.wait()
    return proc.returncode
