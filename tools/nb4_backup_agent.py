# SPDX-License-Identifier: GPL-2.0-only
"""Read-only RAM-agent launch and verified full-unit backups."""
import hashlib
import datetime
import json
import os
from pathlib import Path
import re
import struct
import tempfile
import time

from nb4_rom_flash import (RomFlashError, discover_rom_dfu, dfu_util_path, _device_args,
                           _run, _stream_dfu, validate_full_image, backup_internal, write_backup_metadata)
from nb4_usb_update import Selection, Transport, usb_modules
from nb4_storage_backup import sync_directory
from nb4_update_image import update_package, package_target, VARIANTS

RAM = 0x20010000
RAM_CAPACITY = 128 * 1024 - 512  # Retained radio health record is outside this range.
PRODUCT = 'ApexTX Read-only Backup Agent'
INTERFACE = '@ApexTX Backup /0x90000000/008*1024Ka'
INFO = struct.Struct('<8s10I')
READ_SIZE = 4096
# Exact application fingerprints recognized before loading a board-specific
# RAM agent. Neither a shared PID nor a user's model selection is sufficient.
APPLICATIONS = (
    ('NB4', 742066, '60129d8020ade66b5965a74fd0206c5847d819c28a58ae85f8d01bd072c72b2b'),
    ('NB4', 742122, '9bdc9775082abc4ee080bd915771e450378c5d183f1802788d7d15a6ddc303e2'),
    ('NB4+', 894566, 'd6a17790bd6da52118f9a586104dfa607af00cc5824276ed9f3ae2a70c34ce54'),
    ('NB4+', 894650, '750c9eac7689785e88910ce5ceb72bde2e07d26f019b00c54e8c6cace27e4f1c'),
)


def identify_flash(data):
    if not isinstance(data, bytes) or len(data) != 2097152:
        raise RomFlashError('Read the complete internal flash before loading the backup agent.')
    try:
        # A normal update intentionally preserves an older installed bootloader.
        # Validate the complete application manifest/CRC and target here; the
        # full download-image digest need not match that preserved bootloader.
        return VARIANTS[package_target(update_package(data))]
    except (ValueError, KeyError):
        pass
    matches = {target for target, size, digest in APPLICATIONS
               if hashlib.sha256(data[0x8000:0x8000 + size]).hexdigest() == digest}
    if len(matches) != 1:
        raise RomFlashError('The installed image does not identify supported hardware. No RAM agent was loaded.')
    return matches.pop()


def validate_agent(data, expected_sha256):
    if (not isinstance(data, bytes) or not 512 <= len(data) <= RAM_CAPACITY - 16384 or
            not isinstance(expected_sha256, str) or len(expected_sha256) != 64 or
            hashlib.sha256(data).hexdigest() != expected_sha256):
        raise RomFlashError('The RAM backup agent checksum is invalid.')
    stack, reset = struct.unpack_from('<II', data)
    if stack != RAM + RAM_CAPACITY or not reset & 1 or not RAM <= reset < RAM + len(data):
        raise RomFlashError('The backup agent must execute entirely from its reserved SRAM range.')


def launch(agent, expected_sha256, installed_flash, serial, progress, *, restore=False):
    target = identify_flash(installed_flash)
    validate_agent(agent, expected_sha256)
    if discover_rom_dfu(serial) != serial:
        raise RomFlashError('The identified radio is no longer in ROM DFU. Nothing was written.')
    exe = dfu_util_path()
    with tempfile.TemporaryDirectory(prefix='apextx-ram-agent-') as directory:
        image, check = Path(directory) / 'agent.bin', Path(directory) / 'readback.bin'
        image.write_bytes(agent)
        progress('Loading the NOR restore agent into RAM' if restore else 'Loading the read-only backup agent into RAM', 1)
        # The ROM advertises internal flash, not SRAM, in its DfuSe map.
        # Permit that one validated SRAM range explicitly. No erase,
        # unprotect, option-byte or flash address is ever sent here.
        result = _run([exe, *_device_args(serial), '-a', '0', '-s', f'{hex(RAM)}:force', '-D', str(image)], timeout=30)
        if result.returncode:
            raise RomFlashError('The RAM agent transfer failed. Internal flash and NOR were not written. ' +
                                result.stdout[-2000:])
        result = _run([exe, *_device_args(serial), '-a', '0', '-s', f'{hex(RAM)}:{len(agent)}:force', '-U', str(check)], timeout=30)
        if result.returncode or not check.is_file() or check.read_bytes() != agent:
            raise RomFlashError('The RAM agent readback failed. It was not executed.')
        progress('Read-only RAM agent transfer independently verified', 3)
        try:
            _run([exe, *_device_args(serial), '-a', '0', '-s', f'{hex(RAM)}:force:leave'], timeout=15)
        except Exception:
            # A disconnect during Leave is not evidence of success. The caller
            # must rediscover the distinct agent and verify its identity.
            pass
    return target


def discover(serial, *, product=PRODUCT, interface_name=INTERFACE):
    core, util, backend = usb_modules()
    found = []
    try:
        for device in core.find(find_all=True, backend=backend, idVendor=0x0483, idProduct=0xdf11):
            keep = False
            try:
                if (util.get_string(device, device.iProduct) != product or
                        util.get_string(device, device.iSerialNumber) != serial):
                    continue
                for config in device:
                    for interface in config:
                        if ((interface.bInterfaceClass, interface.bInterfaceSubClass, interface.bInterfaceProtocol) == (0xfe, 1, 2) and
                                util.get_string(device, interface.iInterface) == interface_name):
                            found.append(Selection(device, config.bConfigurationValue,
                                interface.bInterfaceNumber, interface.bAlternateSetting, serial, 0))
                            keep = True
            except (OSError, ValueError):
                # A ROM-to-agent detach can expose an incomplete descriptor
                # briefly. Never select it; the caller has a bounded retry.
                continue
            finally:
                if not keep:
                    util.dispose_resources(device)
        if len(found) > 1:
            raise RomFlashError('Multiple matching backup agents were detected.')
        return found[0] if found else None
    except Exception:
        for selection in found:
            util.dispose_resources(selection.device)
        raise


class AgentTransport(Transport):
    def exit(self):
        self.address(0x91000100)
        self.download(2, b'APXEXIT1')

    def read(self, address, size):
        if type(size) is not int or not 0 < size <= 65536:
            raise RomFlashError('Invalid backup read length.')
        self.address(address)
        result = bytearray()
        for offset in range(0, size, READ_SIZE):
            count = min(READ_SIZE, size - offset)
            chunk = self.incoming(2, count, 2 + offset // READ_SIZE)
            if len(chunk) != count:
                raise RomFlashError('The backup agent returned an incomplete read.')
            result.extend(chunk)
        return bytes(result)


def read_info(transport, serial, *, restore=False):
    data = transport.read(0x91000000, INFO.size)
    if len(data) != INFO.size:
        raise RomFlashError('Invalid backup agent information.')
    magic, protocol, mcu, a, b, c, jedec, nor, internal, entry_ipsr, entry_scr = INFO.unpack(data)
    actual_serial = f'{(a+c) & 0xffffffff:08X}{b >> 16:04X}'
    if magic != (b'APXNOR1\0' if restore else b'APXBKP3\0') or protocol != (4 if restore else 3):
        raise RomFlashError('The RAM agent returned an unsupported information protocol.')
    if mcu & 0xfff != 0x419:
        raise RomFlashError(f'The RAM agent returned an unexpected MCU ID: 0x{mcu:08x}.')
    if actual_serial != serial:
        raise RomFlashError('The RAM agent unit identity changed. No backup was accepted.')
    if nor != 8388608 or internal != 2097152 or jedec & 255 != 0x17:
        raise RomFlashError(f'The RAM agent memory information is unexpected: JEDEC=0x{jedec:06x}, '
                            f'NOR={nor}, internal={internal}. No backup was accepted.')
    return dict(serial=serial, uid=[a,b,c], mcu=mcu, jedec=jedec,
                entry_ipsr=entry_ipsr, entry_scr=entry_scr,
                nor_bytes=nor, internal_bytes=internal)


def capture(transport, installed_flash, serial, directory, progress=lambda *_: None):
    target = identify_flash(installed_flash)
    identity = read_info(transport, serial)
    directory = Path(directory)
    directory.mkdir(mode=0o700, parents=True, exist_ok=False)
    files = {}
    memories = (('internal.bin', 0x08000000, identity['internal_bytes'], 'internal firmware'),
                ('nor.bin', 0x90000000, identity['nor_bytes'], 'external storage'))
    total, completed = sum(item[2] for item in memories) * 2, 0
    for name, base, size, label in memories:
        digest = hashlib.sha256()
        path = directory / name
        with path.open('xb') as stream:
            for offset in range(0, size, 65536):
                block = transport.read(base + offset, min(65536, size - offset))
                if len(block) != min(65536, size - offset):
                    raise RomFlashError('The full-unit backup read was truncated.')
                if name == 'internal.bin' and block != installed_flash[offset:offset + len(block)]:
                    raise RomFlashError('The radio firmware changed after its identity was checked.')
                stream.write(block); digest.update(block)
                completed += len(block)
                progress('Reading ' + label, completed * 99 // total)
            stream.flush(); os.fsync(stream.fileno())
        # A second physical read verifies the complete NOR, not just the
        # checksum of the file already copied to the computer.
        check = hashlib.sha256()
        for offset in range(0, size, 65536):
            block = transport.read(base + offset, min(65536, size - offset))
            check.update(block)
            completed += len(block)
            progress('Verifying ' + label, completed * 99 // total)
        if check.digest() != digest.digest() or hashlib.sha256(path.read_bytes()).digest() != digest.digest():
            raise RomFlashError('Backup verification failed. Do not replace or format any radio storage.')
        files[name] = dict(bytes=size, sha256=digest.hexdigest())
    if read_info(transport, serial) != identity:
        raise RomFlashError('The backup device identity changed.')
    metadata = dict(format='apextx-full-unit-backup-1', raw_nor=True,
                    target=target, identity=identity, files=files)
    with (directory / 'backup.json').open('x', encoding='utf-8') as stream:
        json.dump(metadata, stream, indent=2, sort_keys=True)
        stream.flush(); os.fsync(stream.fileno())
    sync_directory(directory); sync_directory(directory.parent)
    verify(directory)
    progress('Complete firmware and NOR backup verified. Keep it private.', 100)
    return metadata


def verify(directory):
    root = Path(directory)
    manifest = root / 'backup.json'
    if root.is_symlink() or manifest.is_symlink() or manifest.stat().st_size > 4096:
        raise RomFlashError('Invalid full-unit backup manifest.')
    from nb4_maintenance import unique_object
    data = json.loads(manifest.read_text(encoding='utf-8'), object_pairs_hook=unique_object)
    if (not isinstance(data, dict) or data.get('format') != 'apextx-full-unit-backup-1' or
            data.get('raw_nor') is not True):
        raise RomFlashError('This is not a complete internal-flash and NOR backup.')
    identity = data.get('identity')
    if (not isinstance(identity, dict) or
            not isinstance(identity.get('serial'), str) or
            not re.fullmatch('[0-9A-F]{12}', identity['serial']) or
            not isinstance(identity.get('uid'), list) or len(identity['uid']) != 3 or
            any(type(n) is not int or not 0 <= n <= 0xffffffff for n in identity['uid']) or
            type(identity.get('mcu')) is not int or identity['mcu'] & 0xfff != 0x419 or
            type(identity.get('jedec')) is not int or identity['jedec'] & 255 != 0x17 or
            identity.get('nor_bytes') != 8388608 or identity.get('internal_bytes') != 2097152):
        raise RomFlashError('The full-unit backup identity is incomplete or invalid.')
    a, b, c = identity['uid']
    if identity['serial'] != f'{(a+c) & 0xffffffff:08X}{b >> 16:04X}':
        raise RomFlashError('The backup serial and hardware identity disagree.')
    if not isinstance(data.get('files'), dict) or set(data['files']) != {'internal.bin', 'nor.bin'}:
        raise RomFlashError('The full-unit backup is incomplete.')
    for name, size in (('internal.bin',2097152), ('nor.bin',8388608)):
        path = root / name
        if (not isinstance(data['files'][name], dict) or
                path.is_symlink() or not path.is_file() or path.stat().st_size != size or
                data['files'][name].get('bytes') != size or
                hashlib.sha256(path.read_bytes()).hexdigest() != data['files'][name].get('sha256')):
            raise RomFlashError('Full-unit backup checksum verification failed.')
    if identify_flash((root / 'internal.bin').read_bytes()) != data.get('target'):
        raise RomFlashError('Full-unit backup target identification failed.')
    return data


def full_backup(agent, expected_hash, directory, serial=None, progress=lambda *_: None):
    serial = discover_rom_dfu(serial)
    if not serial:
        raise RomFlashError('Connect the radio in STM32 ROM DFU. ApexTX Update cannot launch a RAM agent.')
    image = Path(agent).read_bytes()
    # Validate the package hash before even reading the selected radio.
    validate_agent(image, expected_hash)
    directory = Path(directory)
    if directory.exists() or directory.is_symlink():
        raise RomFlashError('Choose a new backup directory. Existing backups are preserved.')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
    preflight = directory.with_name(directory.name + '-before-agent-' + stamp)
    preflight.mkdir(mode=0o700, parents=True, exist_ok=False)
    internal = preflight / 'internal.bin'
    backup_internal(internal, progress, serial)
    installed = internal.read_bytes()
    target = identify_flash(installed)
    write_backup_metadata(internal, target, serial)
    # Keep the first read even if USB transfer, execution or the NOR read fails.
    progress(f'Internal firmware backup retained at {preflight}', 22)
    launch(image, expected_hash, installed, serial,
           lambda message, percent: progress(message, 22 + percent))
    deadline = time.monotonic() + 15
    selected = None
    while time.monotonic() < deadline:
        selected = discover(serial)
        if selected: break
        time.sleep(.25)
    if selected is None:
        raise RomFlashError('The read-only RAM agent did not enumerate. No MCU or NOR writes were sent. Confirm the radio state before retrying.')
    transport = AgentTransport(selected)
    try:
        result = capture(transport, installed, serial, directory,
                         lambda message, percent: progress(message, 25 + percent * 74 // 100))
        progress('Complete firmware and NOR backup verified. Keep it private.', 100)
        return result
    finally:
        try:
            transport.exit()
        except (OSError, RuntimeError):
            progress('Agent exit was not confirmed. Release Power, then hold it for two seconds to switch off.', 0)
        transport.close()
