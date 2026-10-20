#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Full-image install / recovery for the Noble NB4 over STM32 ROM DFU.

Unlike the normal ApexTX updater (which writes only the application and keeps
the installed bootloader), this writes the complete 2 MiB image, including the
bootloader, to internal flash at 0x08000000 and verifies it by read-back. It is
used for the first installation (from FlySky firmware) and for recovery, and it
drives the bundled `dfu-util` exactly as `tools/nb4-flash.sh` does.
"""
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

USB_ID = '0483:df11'
BOOT_ADDRESS = 0x08000000
FLASH_SIZE = 0x200000
BOOT_SIZE = 0x20000


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
    raise RomFlashError('The bundled dfu-util tool was not found.')


# Hide the console window dfu-util would otherwise flash up on Windows.
_NO_WINDOW = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0


def _run(args, **kw):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, creationflags=_NO_WINDOW, **kw)


def validate_full_image(image):
    """Reject anything that is not a complete NB4 bootloader+application image."""
    if len(image) <= BOOT_SIZE:
        raise RomFlashError('The image is smaller than the reserved bootloader region. '
                            'Select a complete 2 MiB firmware.bin, not an application-only file.')
    if len(image) > FLASH_SIZE:
        raise RomFlashError('The image is larger than the 2 MiB internal flash.')

    def ram(value):
        return 0x10000000 < value <= 0x10010000 or 0x20000000 < value <= 0x20030000

    def code(value, lower, upper):
        return lower <= value < upper and value & 1

    boot_stack, boot_reset = struct.unpack_from('<II', image, 0)
    app_stack, app_reset = struct.unpack_from('<II', image, BOOT_SIZE)
    errors = []
    if not ram(boot_stack):
        errors.append('bootloader stack pointer')
    if not code(boot_reset, 0x08000000, 0x08020000):
        errors.append('bootloader reset vector')
    if not ram(app_stack):
        errors.append('application stack pointer')
    if not code(app_reset, 0x08020000, 0x08000000 + len(image)):
        errors.append('application reset vector')
    if errors:
        raise RomFlashError('This is not a complete ApexTX image for the Noble NB4 '
                            '(invalid ' + ', '.join(errors) + ').')


def image_variant(image):
    """Return 'NB4' / 'NB4+' from the image's own hardware marker, else None.

    Used only to show the user which radio the selected file is for and to ask
    them to confirm it matches theirs: ROM DFU cannot report the radio variant.
    """
    try:
        from nb4_update_image import image_target, VARIANTS
        return VARIANTS.get(image_target(image[BOOT_SIZE:], legacy=True))
    except Exception:
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


def discover_rom_dfu():
    """Return the serial of the single STM32 ROM DFU device, or None."""
    listing = _run([dfu_util_path(), '-l']).stdout
    serials = set()
    for line in listing.splitlines():
        if f'[{USB_ID}]' in line:
            match = re.search(r'serial="([^"]*)"', line)
            serials.add(match.group(1) if match else '')
    if len(serials) > 1:
        raise RomFlashError('More than one STM32 DFU device is connected. '
                            'Connect only the radio you want to flash.')
    return next(iter(serials)) if serials else None


def _device_args(serial):
    args = ['-d', USB_ID]
    if serial:
        args += ['-S', serial]
    return args


def backup_internal(out_path, progress, serial=None):
    """Save the radio's current 2 MiB internal flash (its existing firmware) to
    a file over ROM DFU, so the original firmware can be restored later with the
    install mode. ROM DFU cannot reach the external 8 MiB SPI NOR, so this is a
    firmware backup, not a full factory backup.
    """
    exe = dfu_util_path()
    if serial is None:
        serial = discover_rom_dfu()
    if serial is None:
        raise RomFlashError('No radio in STM32 ROM DFU mode was found. Put the radio into '
                            'ROM DFU (hold the recessed grip button while switching it on) '
                            'and connect USB.')
    out_path = Path(out_path)
    progress('Reading the current firmware from the radio...', 5)
    rc = _stream_dfu([exe, *_device_args(serial), '-a', '0',
                      '-s', f'{hex(BOOT_ADDRESS)}:{FLASH_SIZE}', '-U', str(out_path)],
                     progress, lo=5, hi=98, phase='Backing up')
    if rc != 0 or not out_path.is_file() or out_path.stat().st_size != FLASH_SIZE:
        raise RomFlashError('The backup could not be read. Nothing on the radio was changed.')
    progress(f'Backup saved ({FLASH_SIZE // 1024} KiB). Keep this file to restore later.', 100)


def run_install(image, progress, serial=None):
    """Write, verify and launch a complete image. `progress(message, percent)`."""
    validate_full_image(image)
    exe = dfu_util_path()
    if serial is None:
        serial = discover_rom_dfu()
    if serial is None:
        raise RomFlashError('No radio in STM32 ROM DFU mode was found. Put the radio into '
                            'ROM DFU (hold the recessed grip button while switching it on) '
                            'and connect USB.')
    size = len(image)
    tmp = Path(sys.argv[0]).resolve().parent if getattr(sys, 'frozen', False) else Path('.')
    work = Path(_writable_tmp())
    image_path = work / 'nb4_install_image.bin'
    read_path = work / 'nb4_install_readback.bin'
    image_path.write_bytes(image)
    try:
        progress('Writing the complete image to the radio...', 5)
        wr = _stream_dfu([exe, *_device_args(serial), '-a', '0',
                          '-s', hex(BOOT_ADDRESS), '-D', str(image_path)],
                         progress, lo=5, hi=70, phase='Writing')
        if wr != 0:
            raise RomFlashError('Writing the image failed. The radio stays in DFU; '
                                'reconnect and try again with a known-good cable.')
        progress('Reading the image back to verify...', 72)
        rd = _stream_dfu([exe, *_device_args(serial), '-a', '0',
                          '-s', f'{hex(BOOT_ADDRESS)}:{size}', '-U', str(read_path)],
                         progress, lo=72, hi=94, phase='Verifying')
        if rd != 0 or not read_path.is_file():
            raise RomFlashError('Reading the image back failed. The radio stays in DFU.')
        if read_path.read_bytes() != image:
            raise RomFlashError('The read-back does not match the image. Nothing was started; '
                                'the radio stays in DFU. Retry with a known-good image and cable.')
        progress('Verified. Finishing up...', 96)
        # The STM32 ROM rejects a plain DFU DETACH; a DfuSe "leave" ends DFU mode
        # without erasing or rewriting the verified flash. On the NB4 the radio
        # usually needs a manual power cycle afterwards to boot, so we always ask
        # for one rather than claim it restarts on its own.
        _run([exe, *_device_args(serial), '-a', '0', '-s', f'{hex(BOOT_ADDRESS)}:leave'])
        done = 'Installation complete and verified. Switch the radio OFF (hold the power ' \
               'button until it is fully off), then switch it ON again to start it.'
        for _ in range(8):
            time.sleep(1)
            if discover_rom_dfu() != serial:
                break
        progress(done, 100)
    finally:
        for path in (image_path, read_path):
            try:
                path.unlink()
            except OSError:
                pass


def _writable_tmp():
    import tempfile
    return tempfile.mkdtemp(prefix='apextx-install-')


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
                mapped = lo + (hi - lo) * pct // 100
                if mapped != last:
                    last = mapped
                    progress(f'{phase} {pct}%', mapped)
            buf = ''
    proc.wait()
    return proc.returncode
