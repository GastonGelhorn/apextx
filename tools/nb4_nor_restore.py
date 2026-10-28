# SPDX-License-Identifier: GPL-2.0-only
"""Explicit complete external-NOR restoration; never writes internal flash."""
import hashlib
import io
import json
from pathlib import Path
import secrets
import struct
import time

from nb4_backup_agent import AgentTransport, discover, launch, read_info, validate_agent, verify
from nb4_rf_transfer import save_json
from nb4_rom_flash import RomFlashError, discover_rom_dfu

PRODUCT = 'ApexTX NOR Restore Agent'
INTERFACE = '@ApexTX NOR Restore /0x92000000/001*016Kg'
SECTOR = 4096
STATUS = struct.Struct('<IIII16s')


def identity_matches(current, backup):
    if any(current.get(key) != backup.get(key) for key in
           ('serial', 'uid', 'mcu', 'jedec', 'nor_bytes', 'internal_bytes')):
        raise RomFlashError('The restore agent and backup belong to different hardware. Nothing was restored.')
    if current['jedec'] != 0xc84017:
        raise RomFlashError('This NOR chip has not been enabled for restoration.')


def status(transport, nonce):
    raw = transport.read(0x92000200, STATUS.size)
    if len(raw) != STATUS.size:
        raise RomFlashError('The restore agent returned incomplete status.')
    phase, error, completed, changed, actual = STATUS.unpack(raw)
    if actual != nonce or phase > 8 or error > 5 or not 0 <= changed <= completed + 1 <= 2049:
        raise RomFlashError('The NOR restore operation identity or progress changed.')
    if error or phase == 8:
        raise RomFlashError(f'External-NOR restoration stopped (error {error}, sector {completed}). Keep the backup and restart restoration from ROM DFU.')
    return phase, completed, changed


def send(transport, address, data):
    transport.address(address)
    transport.download(2, data)


def restore_connected(transport, backup, directory, *, confirmed=False,
                      progress=lambda *_: None, clock=time.monotonic, pause=time.sleep):
    if not confirmed:
        raise RomFlashError('Confirm the exact unit and complete external-storage replacement first.')
    backup = Path(backup)
    metadata = verify(backup)
    image = (backup/'nor.bin').read_bytes()
    if (len(image) != metadata['identity']['nor_bytes'] or
            hashlib.sha256(image).hexdigest() != metadata['files']['nor.bin']['sha256']):
        raise RomFlashError('The verified backup changed before restoration.')
    serial = metadata['identity']['serial']
    current = read_info(transport, serial, restore=True)
    identity_matches(current, metadata['identity'])
    directory = Path(directory)
    directory.mkdir(mode=0o700, parents=True, exist_ok=False)
    nonce = secrets.token_bytes(16)
    record = dict(format='apextx-nor-restoration-1', serial=serial, target=metadata['target'],
                  backup=str(backup.resolve()), backup_sha256=hashlib.sha256((backup/'backup.json').read_bytes()).hexdigest(),
                  nonce=nonce.hex(), state='arming', write_started=False, completed=0, changed=0)
    receipt = directory/'restore.json'
    save_json(receipt, record)
    try:
        arm = struct.pack('<8s5I32s16s', b'APXNOR1', *current['uid'], current['jedec'],
                          current['nor_bytes'], bytes.fromhex(metadata['files']['nor.bin']['sha256']), nonce)
        send(transport, 0x92000000, arm)
        if status(transport, nonce) != (1, 0, 0):
            raise RomFlashError('The agent did not start an empty restoration.')
        with io.BytesIO(image) as stream:
            for index in range(current['nor_bytes']//SECTOR):
                block = stream.read(SECTOR)
                if len(block) != SECTOR:
                    raise RomFlashError('The verified backup changed during restoration.')
                send(transport, 0x92000100, struct.pack('<8s16sI32s', b'APXBLK1', nonce,
                                                      index, hashlib.sha256(block).digest()))
                send(transport, 0x92001000, block)
                # A lost COMMIT response can mean an erase already began.
                # Record that intent first; never blindly replay COMMIT.
                record.update(state='restoring', write_started=True)
                save_json(receipt, record)
                send(transport, 0x92000180, struct.pack('<8s16s', b'APXCMT1', nonce))
                deadline = clock() + 8
                while True:
                    phase, completed, changed = status(transport, nonce)
                    if phase in (1, 7) and completed == index + 1:
                        break
                    if completed != index or clock() >= deadline:
                        raise RomFlashError('The restore sector was not independently verified. Restart from ROM DFU; do not assume it completed.')
                    pause(.01)
                record.update(completed=completed, changed=changed)
                save_json(receipt, record)
                progress('Restoring and verifying external storage', completed * 85 // 2048)
        if status(transport, nonce)[0] != 7:
            raise RomFlashError('The restore agent did not verify the complete NOR hash.')
        record['state'] = 'verifying'; save_json(receipt, record)
        check = hashlib.sha256()
        for offset in range(0, current['nor_bytes'], 65536):
            block = transport.read(0x90000000+offset,65536)
            if len(block) != 65536: raise RomFlashError('The restored NOR readback was truncated.')
            check.update(block)
            progress('Independently reading restored external storage', 85+(offset+65536)*14//current['nor_bytes'])
        if check.hexdigest() != metadata['files']['nor.bin']['sha256']:
            raise RomFlashError('The restored NOR checksum does not match the private backup.')
        identity_matches(read_info(transport,serial,restore=True), current)
        record.update(state='complete', readback_verified=True)
        save_json(receipt,record)
        progress('Complete external storage restored and independently verified',100)
        return record
    except Exception:
        record['state'] = 'recovery_required' if record['write_started'] else 'failed_before_write'
        save_json(receipt,record)
        raise


def run(agent, expected_hash, backup, directory, serial, *, confirmed=False, progress=lambda *_: None):
    if not confirmed:
        raise RomFlashError('Confirm the exact unit before loading the restore agent.')
    metadata = verify(backup)
    if metadata['identity']['serial'] != serial:
        raise RomFlashError('The backup belongs to another transmitter.')
    image = Path(agent).read_bytes(); validate_agent(image,expected_hash)
    if b'APXNOR1\0' not in image:
        raise RomFlashError('Select the separate NOR restore agent, not the read-only backup agent.')
    if discover_rom_dfu(serial) != serial:
        raise RomFlashError('Connect the selected transmitter in ROM DFU for complete storage restoration.')
    # Loading SRAM does not erase either memory. The actual UID/MCU/JEDEC is
    # checked again by the host and agent before a sector can be committed.
    launch(image,expected_hash,(Path(backup)/'internal.bin').read_bytes(),serial,progress,restore=True)
    deadline = time.monotonic()+15
    selected = None
    while time.monotonic() < deadline:
        selected = discover(serial,product=PRODUCT,interface_name=INTERFACE)
        if selected: break
        time.sleep(.25)
    if not selected: raise RomFlashError('The NOR restore agent did not enumerate; no NOR write was requested.')
    transport = AgentTransport(selected)
    try:
        return restore_connected(transport,backup,directory,confirmed=True,progress=progress)
    finally:
        try: transport.exit()
        except (OSError,RuntimeError): pass
        transport.close()
