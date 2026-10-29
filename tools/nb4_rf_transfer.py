# SPDX-License-Identifier: GPL-2.0-only
"""Verified RF staging and explicitly confirmed maintenance operations."""
import hashlib
import json
import os
from pathlib import Path
import secrets
import struct
import time

from nb4_maintenance import (Client, MaintenanceError, CAPABILITIES, INVENTORY,
                             PREPARE, UPLOAD, START, STATUS, CANCEL, RECOVER, STORAGE_SESSION,
                             ASSET_BEGIN, ASSET_DATA, REBOOT_UPDATE, MAX_PAYLOAD, validate_inventory)
from nb4_rf_catalog import ANY_IDENTITY, IMAGES, UPDATE_HARDWARE, image_name
from nb4_storage_backup import verify_backup, sync_directory

ASSET_CHUNK = 512
assert ASSET_CHUNK + 5 <= MAX_PAYLOAD


class TransferClient(Client):
    # Unlike the default inventory client, this is instantiated only inside an
    # explicit maintenance operation after the USB identity has been selected.
    allowed_commands = (CAPABILITIES, INVENTORY, STORAGE_SESSION, PREPARE, UPLOAD, START, STATUS, CANCEL, RECOVER)


class AssetClient(Client):
    # The one-click flasher path stores catalog files and restarts the radio;
    # it cannot prepare, start or recover an RF write.
    allowed_commands = (CAPABILITIES, INVENTORY, ASSET_BEGIN, ASSET_DATA, REBOOT_UPDATE)


def installable_images(target):
    """Catalog indices the radio installs by itself on this target: the same
    production rule (transfer and recovery verified) as the firmware applies."""
    number = {'nb4': 1, 'nb4plus': 2}.get(target)
    if number is None:
        raise MaintenanceError('Select one identified ApexTX radio first.')
    return sorted({rule['image'] for rule in UPDATE_HARDWARE
                   if rule['target'] == number and rule['transfer'] and rule['recovery']})


def validate_asset(value, index, size):
    if (not isinstance(value, dict) or value.get('asset') != index or
            type(value.get('verified')) is not bool or value.get('bytes') != size or
            type(value.get('uploaded')) is not int or not 0 <= value['uploaded'] <= size):
        raise MaintenanceError('Invalid RF asset progress.')
    return value


def publish_assets(client, target, images, *, progress=lambda *_: None):
    """Store every installable catalog image on the radio's card.

    `images` is the file map of a bundle that verify_bundle() accepted. Each
    image is hashed again here, and the radio hashes it independently before
    the file becomes visible; a present, verified image is never re-sent. This
    never writes an RF component: installation stays with the radio.
    """
    capabilities = client.request(CAPABILITIES)
    if capabilities.get('protocol') != 1 or capabilities.get('assets') is not True:
        raise MaintenanceError('This radio build does not accept RF catalog assets.')
    results = []
    for index in installable_images(target):
        image = IMAGES[index]
        data = images.get(image_name(image))
        if (not isinstance(data, bytes) or len(data) != image['bytes'] or
                hashlib.sha256(data).hexdigest() != image['sha256']):
            raise MaintenanceError('RF image verification failed before transfer.')
        try:
            value = validate_asset(client.request(ASSET_BEGIN, bytes([index])), index, len(data))
            if value['verified']:
                results.append(dict(image=index, label=image['label'], bytes=len(data), state='present'))
                continue
            if value['uploaded'] != 0:
                raise MaintenanceError('The radio did not start an empty asset upload.')
            for offset in range(0, len(data), ASSET_CHUNK):
                block = data[offset:offset + ASSET_CHUNK]
                value = validate_asset(client.request(ASSET_DATA, struct.pack('<BI', index, offset) + block),
                                       index, len(data))
                if value['uploaded'] != offset + len(block):
                    raise MaintenanceError('RF asset progress does not match the transferred file.')
                progress(f"Publishing RF image {image['label']}", value['uploaded'] * 100 // len(data))
            if not value['verified']:
                raise MaintenanceError('The radio did not verify the stored RF image.')
        except MaintenanceError as error:
            raise MaintenanceError(f"{image['label']}: {error}") from error
        results.append(dict(image=index, label=image['label'], bytes=len(data), state='uploaded'))
    return results


def reboot_into_update(client):
    """Ask the radio to restart into ApexTX Update. The reply precedes the
    restart; the caller then waits for the update device to enumerate."""
    if client.request(CAPABILITIES).get('reboot_update') is not True:
        raise MaintenanceError('This radio build cannot restart into update mode from the flasher.')
    if client.request(REBOOT_UPDATE).get('reboot_update') is not True:
        raise MaintenanceError('The radio did not confirm the restart into update mode.')


def save_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, sort_keys=True, indent=2, allow_nan=False)
        stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    os.replace(temporary, path)
    sync_directory(path.parent)


def plan(inventory, index):
    if type(index) is not int or not 0 <= index < len(IMAGES):
        raise MaintenanceError('Select a catalog image.')
    validate_inventory(inventory, inventory.get('serial'), inventory.get('target'))
    image = IMAGES[index]
    current = inventory[image['kind']]
    state = 'unknown'
    reason = 'A fresh component identity is required.'
    if image['kind']=='receiver' and (not inventory['connected'] or not inventory['two_way']):
        reason = 'Bind and connect the receiver with two-way telemetry before preparing an update.'
    elif (current['valid'] and current['product'] == image.get('reported_product', image['product']) and
            (image['kind'] == 'receiver' or current.get('firmware_known'))):
        if image['kind'] == 'receiver':
            words = current['identity_words']
            hardware, rf_hardware, bootloader, library = words[0], words[1], words[2], words[4]
        else:
            hardware, rf_hardware, bootloader, library = current['hardware'], 0, 0, current['rf_library']
        target = {'nb4':1, 'nb4plus':2}.get(inventory['target'])
        if target is None:
            raise MaintenanceError('The inventory does not identify a supported transmitter.')
        compatible = any(rule['target'] == target and rule['image'] == index and
                         all(expected in (ANY_IDENTITY, reported) for expected, reported in
                             zip((rule['hardware'],rule['rf_hardware'],rule['bootloader'],rule['rf_library']),
                                 (hardware,rf_hardware,bootloader,library)))
                         for rule in UPDATE_HARDWARE)
        if compatible:
            state = ('current' if current['firmware'] == image['version'] else
                     'ahead' if current['firmware'] > image['version'] else 'available')
            reason = 'No downgrade is permitted.' if state == 'ahead' else ''
        else:
            state = 'incompatible'
            reason = ('This product ID is shared. The hardware identity does not match this image.'
                      if index == 2 else 'The hardware identity does not match this image.')
    elif current['valid'] and current['product'] != image.get('reported_product', image['product']):
        state = 'incompatible'
        reason = 'This image is for a different RF component.'
    return dict(component=image['kind'], image=index,
                product=image.get('reported_product', image['product']),
                current=current['firmware'], proposed=image['version'], state=state,
                required=False, write_supported=state in ('available','current') and current.get('write_supported') is True,
                reason=reason)


def validate_status(value, operation, size):
    if not isinstance(value, dict) or value.get('operation') != operation:
        raise MaintenanceError('The radio reported a different RF operation.')
    for key, maximum in (('operation', 0xffffffff), ('phase', 7), ('error_code', 7),
                         ('uploaded', size), ('transferred', size)):
        if type(value.get(key)) is not int or not 0 <= value[key] <= maximum:
            raise MaintenanceError('Invalid RF maintenance progress.')
    for key in ('validated', 'cancellable', 'driving_locked'):
        if type(value.get(key)) is not bool:
            raise MaintenanceError('Invalid RF maintenance state.')
    if (value['phase'] == 5 and
            (value['error_code'] or value['driving_locked'] or value['cancellable'] or
             value['transferred'] != size)):
        raise MaintenanceError('The radio did not confirm a complete, unlocked RF operation.')
    if value['phase'] == 7 and not value['driving_locked']:
        raise MaintenanceError('An interrupted RF write must keep driving locked.')
    return value


class RfTransfer:
    def __init__(self, stream, serial, target, directory):
        self.client = TransferClient(stream)
        self.serial, self.target = serial, target
        self.directory = Path(directory)
        self.record = None

    def inventory(self):
        return validate_inventory(self.client.request(INVENTORY), self.serial, self.target)

    def backup_hash(self, backup):
        if (Path(backup) / 'backup.json').is_file():
            from nb4_backup_agent import verify
            full = verify(backup)
            if (full['identity']['serial'] != self.serial or
                    full['target'] != {'nb4':'NB4','nb4plus':'NB4+'}[self.target]):
                raise MaintenanceError('The backup belongs to a different radio.')
            receipt = Path(backup) / 'backup.json'
        else:
            metadata=verify_backup(backup)
            owner=metadata.get('device')
            if owner is None:
                owner_path = Path(backup) / 'device.json'
                if owner_path.is_symlink() or not owner_path.is_file() or owner_path.stat().st_size > 4096:
                    raise MaintenanceError('A backup bound to this radio is required.')
                owner = json.loads(owner_path.read_text(encoding='utf-8'))
            if owner.get('serial') != self.serial or owner.get('target') != self.target:
                raise MaintenanceError('The backup belongs to a different radio.')
            receipt = Path(backup) / 'storage.json'
        return hashlib.sha256(receipt.read_bytes()).digest()

    def check_backup(self):
        if self.backup_hash(self.record['backup']).hex() != self.record['backup_sha256']:
            raise MaintenanceError('The verified recovery backup changed. Nothing was started.')

    def stage(self, index, data, backup, *, progress=lambda *_: None):
        capabilities = self.client.request(CAPABILITIES)
        if capabilities.get('protocol') != 1 or capabilities.get('file_transfer') is not True:
            raise MaintenanceError('Install a radio build with verified RF staging first.')
        inventory = self.inventory()
        image = IMAGES[index] if type(index) is int and 0 <= index < len(IMAGES) else None
        if (image is None or not isinstance(data, bytes) or len(data) != image['bytes'] or
                hashlib.sha256(data).hexdigest() != image['sha256']):
            raise MaintenanceError('RF image verification failed before transfer.')
        current = plan(inventory, index)
        if current['state'] not in ('available', 'current'):
            raise MaintenanceError(current['reason'] or 'This RF image does not match an identified compatible component.')
        backup_hash = self.backup_hash(backup)
        operation = secrets.randbelow(0xffffffff) + 1
        self.directory.mkdir(mode=0o700, parents=True, exist_ok=False)
        self.record = dict(format='apextx-rf-operation-1', operation=operation,
                           serial=self.serial, target=self.target, inventory=inventory,
                           image=index, sha256=image['sha256'], bytes=image['bytes'],
                           backup=str(Path(backup).resolve()), backup_sha256=backup_hash.hex(),
                           state='preparing', write_started=False)
        self.save()
        prepare = struct.pack('<BIII32s', index, operation, inventory['epoch'],
                              inventory['car_key'], backup_hash)
        value = validate_status(self.client.request(PREPARE, prepare), operation, len(data))
        if value['uploaded'] != 0:
            raise MaintenanceError('The radio did not start an empty staging operation.')
        for offset in range(0, len(data), 1000):
            block = data[offset:offset + 1000]
            value = validate_status(self.client.request(UPLOAD, struct.pack('<II', operation, offset) + block), operation, len(data))
            if value['uploaded'] != offset + len(block):
                raise MaintenanceError('RF staging progress does not match the transferred file.')
            progress('Preparing verified RF image', value['uploaded'] * 100 // len(data))
        if not value['validated']:
            raise MaintenanceError('The radio did not independently verify the RF image.')
        if self.inventory() != inventory:
            raise MaintenanceError('The RF identity changed during staging. Nothing was flashed.')
        self.record.update(state='staged', status=value)
        self.save()
        return dict(plan=current, status=value)

    def save(self):
        save_json(self.directory / 'operation.json', self.record)

    def cancel(self):
        if not self.record:
            raise MaintenanceError('Load the RF operation before cancelling it.')
        value = validate_status(self.client.request(CANCEL, struct.pack('<I', self.record['operation'])),
                                self.record['operation'], self.record['bytes'])
        if value['phase'] != 6 or value['driving_locked'] or value['transferred']:
            raise MaintenanceError('The radio did not confirm cancellation before writing.')
        self.record.update(state='cancelled', write_started=False, status=value)
        self.save()
        return value

    def load(self):
        path = self.directory / 'operation.json'
        if path.is_symlink() or path.stat().st_size > 16384:
            raise MaintenanceError('Invalid RF operation journal.')
        value = json.loads(path.read_text(encoding='utf-8'))
        index = value.get('image')
        if (value.get('format') != 'apextx-rf-operation-1' or value.get('serial') != self.serial or
                value.get('target') != self.target or type(index) is not int or not 0 <= index < len(IMAGES) or
                value.get('sha256') != IMAGES[index]['sha256'] or value.get('bytes') != IMAGES[index]['bytes'] or
                type(value.get('operation')) is not int or not 1 <= value['operation'] <= 0xffffffff or
                type(value.get('write_started')) is not bool or
                value.get('state') not in ('preparing','staged','starting','running','complete','cancelled',
                                          'failed_before_write','recovering','recovery_required')):
            raise MaintenanceError('The RF journal does not match this unit and image.')
        validate_inventory(value.get('inventory'), self.serial, self.target)
        self.record = value
        return value

    def reconcile(self):
        """Inspect an ambiguous disconnect without repeating START or RECOVER."""
        if not self.record:
            raise MaintenanceError('Load the RF operation before checking its result.')
        value = validate_status(self.client.request(STATUS, struct.pack('<I', self.record['operation'])),
                                self.record['operation'], self.record['bytes'])
        current = self.inventory()
        if current['car_key'] != self.record['inventory']['car_key']:
            raise MaintenanceError('The bound receiver configuration changed. The saved RF result was not accepted.')
        phase = value['phase']
        state = 'recovery_required'
        if phase == 5:
            decision = plan(current, self.record['image'])
            if decision['state'] != 'current':
                raise MaintenanceError('The completed RF operation needs a fresh matching hardware and version reading.')
            state = 'complete'
            self.record['verified_inventory'] = current
        elif phase == 1 and value['cancellable'] and not value['driving_locked'] and not value['transferred']:
            state = 'staged' if value['validated'] and value['uploaded']==self.record['bytes'] else 'failed_before_write'
        elif phase == 6 and not value['driving_locked'] and not value['transferred']:
            state = 'cancelled'
        elif phase in (2, 3, 4):
            state = 'running'
        self.record.update(state=state, status=value)
        self.save()
        return value

    def recover(self, *, confirmed=False, progress=lambda *_: None, clock=time.monotonic):
        if not confirmed or not self.record:
            raise MaintenanceError('Review the interrupted RF operation before recovery.')
        if self.client.request(CAPABILITIES).get('rf_recover') is not True:
            raise MaintenanceError('Enable the matching qualified recovery build on this radio.')
        self.check_backup()
        current = self.inventory()
        if current['car_key'] != self.record['inventory']['car_key']:
            raise MaintenanceError('The bound receiver configuration changed. Recovery was not started.')
        operation, size = self.record['operation'], self.record['bytes']
        state = validate_status(self.client.request(STATUS, struct.pack('<I', operation)), operation, size)
        if state['phase'] != 7:
            raise MaintenanceError('The radio does not require this recovery operation. Do not repeat START.')
        self.record.update(state='recovering'); self.save()
        value = validate_status(self.client.request(RECOVER, struct.pack('<I', operation)), operation, size)
        return self._monitor(value, plan(self.record['inventory'], self.record['image']), progress, clock,
                             recovery_entry=True)

    def _monitor(self, value, decision, progress, clock, *, recovery_entry=False):
        operation, size = self.record['operation'], self.record['bytes']
        try:
            began = clock()
            deadline = began + 360
            # RECOVER acknowledges the request before the radio finishes its
            # bounded image hash check. Keep the connection open and only poll;
            # never repeat RECOVER while waiting for the phase to advance.
            entry_deadline = began + 10
            while value['phase'] not in (5, 6, 7) or (
                    recovery_entry and value['phase'] == 7 and clock() < entry_deadline):
                if clock() >= deadline:
                    raise MaintenanceError('RF operation timed out; inspect System > Update before recovery.')
                value = validate_status(self.client.request(STATUS, struct.pack('<I', operation)), operation, size)
                if value['phase'] != 7:
                    recovery_entry = False
                self.record.update(state='recovering' if recovery_entry else 'running', status=value); self.save()
                progress('Updating RF component', min(99, value['transferred'] * 99 // size))
                time.sleep(.2)
            if value['phase'] == 6:
                self.record.update(state='failed_before_write', write_started=False, status=value); self.save()
                raise MaintenanceError('RF preparation failed or was cancelled before writing.')
            if value['phase'] != 5:
                raise MaintenanceError('RF update needs recovery. Keep the radio powered and retain this backup.')
            verify_deadline = clock() + 15
            while True:
                after = self.inventory()
                if after['car_key'] != self.record['inventory']['car_key']:
                    raise MaintenanceError('The bound receiver configuration changed before final verification.')
                verified = plan(after, self.record['image'])
                if verified['state'] == 'current':
                    break
                if verified['state'] != 'unknown' or clock() >= verify_deadline:
                    raise MaintenanceError('The installed RF version could not be verified.')
                progress('Checking the installed RF version', 99)
                time.sleep(.2)
            self.record.update(state='complete', status=value, verified_inventory=after); self.save()
            progress('RF component verified', 100)
            return value
        except Exception:
            if self.record['state'] != 'failed_before_write': self.record.update(state='recovery_required')
            self.save()
            raise

    def start(self, *, confirmed=False, reinstall=False, progress=lambda *_: None, clock=time.monotonic):
        if not confirmed or not self.record or self.record['state'] != 'staged':
            raise MaintenanceError('Review and confirm the staged RF plan first.')
        current = self.inventory()
        if current != self.record['inventory']:
            raise MaintenanceError('The radio or receiver changed. Prepare a new plan.')
        decision = plan(current, self.record['image'])
        if (decision['state'] != 'available' and not (reinstall and decision['state'] == 'current')) or not decision['write_supported']:
            raise MaintenanceError('This RF hardware is not qualified for writing, or is already current.')
        if self.client.request(CAPABILITIES).get('rf_write') is not True:
            raise MaintenanceError('This build does not enable RF writing.')
        self.check_backup()
        operation, size = self.record['operation'], self.record['bytes']
        # Persist before START: if its reply is lost, its effect is unknown.
        self.record.update(state='starting', write_started=True); self.save()
        try:
            value = validate_status(self.client.request(START, struct.pack('<IB', operation, int(reinstall))), operation, size)
            return self._monitor(value, decision, progress, clock)
        except Exception:
            if self.record['state'] != 'failed_before_write': self.record.update(state='recovery_required')
            self.save()
            raise
