# SPDX-License-Identifier: GPL-2.0-only
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import nb4_rf_transfer as rf
import nb4_maintenance as wire
from test_nb4_maintenance import inventory


class Radio:
    def __init__(self):
        self.commands, self.pending, self.data = [], b'', b''
        self.can_write = False
        self.inventory = inventory()
        self.inventory['receiver'].update(product=0x12340004, firmware=0x112,
             identity_words=[0x104,0x104,0x103,0x112,0x301], write_supported=False)
        self.status = dict(operation=0, phase=0, error_code=0, uploaded=0,
                           transferred=0, validated=False, cancellable=True, driving_locked=False)

    def write(self, frame):
        cmd, request, payload = wire.Decoder().feed(frame)[0]
        self.commands.append(cmd)
        if cmd == wire.CAPABILITIES:
            result = dict(protocol=1, inventory=True, file_transfer=True,
                          rf_write=self.can_write, rf_recover=self.can_write)
        elif cmd == wire.INVENTORY:
            result = self.inventory
        elif cmd == wire.PREPARE:
            _, operation, epoch, car, backup = struct.unpack('<BIII32s', payload)
            assert (epoch, car) == (3,4) and any(backup)
            self.status.update(operation=operation); result=self.status
        elif cmd == wire.UPLOAD:
            operation, offset = struct.unpack_from('<II', payload)
            assert operation == self.status['operation'] and offset == len(self.data)
            self.data += payload[8:]
            self.status.update(uploaded=len(self.data), validated=True); result=self.status
        elif cmd == wire.START:
            assert self.can_write and struct.unpack('<IB',payload)[0] == self.status['operation']
            self.status.update(phase=2,driving_locked=True,cancellable=False);result=self.status
        elif cmd == wire.CANCEL:
            assert struct.unpack('<I',payload)[0] == self.status['operation']
            self.status.update(phase=6,driving_locked=False,cancellable=False,validated=False)
            result=self.status
        elif cmd == wire.STATUS:
            self.status.update(phase=5,transferred=len(self.data),driving_locked=False,cancellable=False)
            self.inventory['epoch'] += 1
            self.inventory['receiver']['firmware']=0x113
            self.inventory['receiver']['identity_words'][3]=0x113
            result=self.status
        else:
            raise AssertionError(f'unexpected write command: {cmd}')
        self.pending += wire.encode(cmd | 0x80, request, json.dumps(result).encode())
        return len(frame)

    def read(self, size):
        data, self.pending = self.pending[:size], self.pending[size:]
        return data


class RfTransferTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name); self.backup = self.root / 'backup'; self.backup.mkdir()
        (self.backup / 'device.json').write_text(json.dumps(dict(serial='000000000001',target='nb4')))
        (self.backup / 'storage.json').write_text('{}')
        self.data = b'a' * 3001
        image = dict(rf.IMAGES[1], bytes=len(self.data), sha256=hashlib.sha256(self.data).hexdigest())
        self.catalog = patch.object(rf, 'IMAGES', (rf.IMAGES[0],image,rf.IMAGES[2]))
        self.catalog.start(); self.addCleanup(self.catalog.stop)
        self.peer = Radio()
        self.transfer = rf.RfTransfer(self.peer, '000000000001','nb4',self.root / 'operation')

    def test_stages_only_after_backup_and_independently_verified_bytes(self):
        with patch.object(rf, 'verify_backup', return_value={}) as verify:
            self.transfer.stage(1, self.data, self.backup)
        verify.assert_called_once_with(self.backup)
        self.assertEqual(self.peer.data, self.data)
        self.assertEqual(self.peer.commands.count(wire.UPLOAD), 4)
        record = json.loads((self.root / 'operation/operation.json').read_text())
        self.assertFalse(record['write_started']); self.assertEqual(record['state'], 'staged')
        self.assertNotIn(wire.START, self.peer.commands)
        with self.assertRaisesRegex(wire.MaintenanceError, 'not qualified'):
            self.transfer.start(confirmed=True)
        self.assertNotIn(wire.START, self.peer.commands)

    def test_wrong_image_backup_identity_and_corrupt_backup_never_prepare(self):
        with patch.object(rf, 'verify_backup', return_value={}):
            with self.assertRaises(wire.MaintenanceError):
                self.transfer.stage(1, b'wrong', self.backup)
            (self.backup / 'device.json').write_text(json.dumps(dict(serial='other',target='nb4')))
            with self.assertRaises(wire.MaintenanceError):
                self.transfer.stage(1, self.data, self.backup)
        with patch.object(rf, 'verify_backup', side_effect=RuntimeError('backup damaged')):
            with self.assertRaises(RuntimeError): self.transfer.stage(1, self.data, self.backup)
        self.assertNotIn(wire.PREPARE, self.peer.commands)

    def test_cancellation_is_persisted_only_after_the_radio_confirms_no_write(self):
        with patch.object(rf,'verify_backup',return_value={}):
            self.transfer.stage(1,self.data,self.backup)
        unsafe = dict(self.peer.status,phase=7,driving_locked=True)
        with patch.object(self.transfer.client,'request',return_value=unsafe):
            with self.assertRaises(wire.MaintenanceError):self.transfer.cancel()
        self.assertEqual(self.transfer.record['state'],'staged')
        self.assertEqual(self.transfer.cancel()['phase'],6)
        record = self.transfer.load()
        self.assertEqual(record['state'],'cancelled')
        self.assertFalse(record['write_started'])
        with self.assertRaises(wire.MaintenanceError):self.transfer.start(confirmed=True)
        self.assertNotIn(wire.START,self.peer.commands)

    def test_downgrade_and_shared_id_cannot_be_authorized_by_a_label(self):
        value = self.peer.inventory
        value['receiver']['firmware']=0x114; value['receiver']['identity_words'][3]=0x114
        self.assertEqual(rf.plan(value,1)['state'],'ahead')
        value['receiver']['product']=0x12340006
        self.assertIn('shared', rf.plan(value,2)['reason'])
        self.assertFalse(rf.plan(value,2)['write_supported'])

    def test_hardware_revision_is_checked_before_staging_a_shared_product(self):
        self.peer.inventory['receiver']['identity_words'][2] = 0x107
        with patch.object(rf,'verify_backup',return_value={}):
            with self.assertRaises(wire.MaintenanceError):self.transfer.stage(1,self.data,self.backup)
        self.assertNotIn(wire.PREPARE,self.peer.commands)
        receiver = self.peer.inventory['receiver']
        receiver.update(product=0x12340006,firmware=0x111,write_supported=True,
                        identity_words=[0x104,0x104,0x103,0x111,0x301])
        self.assertEqual(rf.plan(self.peer.inventory,2)['state'],'incompatible')
        self.assertFalse(rf.plan(self.peer.inventory,2)['write_supported'])
        receiver['identity_words'][2] = 0x107
        self.assertEqual(rf.plan(self.peer.inventory,2)['state'],'current')

    def test_status_rejects_late_foreign_and_coerced_values(self):
        value = dict(self.peer.status, operation=3)
        self.assertEqual(rf.validate_status(value,3,100),value)
        for key, bad in (('operation',4),('phase',True),('phase',8),('error_code',8),('uploaded',101),
                         ('transferred',-1),('cancellable',1)):
            with self.subTest(key=key), self.assertRaises(wire.MaintenanceError):
                rf.validate_status(dict(value,**{key:bad}),3,100)
        complete = dict(value,phase=5,transferred=100,cancellable=False,driving_locked=False)
        self.assertEqual(rf.validate_status(complete,3,100),complete)
        for key,bad in (('error_code',1),('transferred',99),('driving_locked',True),('cancellable',True)):
            with self.subTest(key=key),self.assertRaises(wire.MaintenanceError):
                rf.validate_status(dict(complete,**{key:bad}),3,100)
        with self.assertRaises(wire.MaintenanceError):
            rf.validate_status(dict(value,phase=7,driving_locked=False),3,100)

    def test_catalog_header_is_generated_from_the_host_allowlist(self):
        from nb4_rf_catalog import firmware_catalog_header
        path=Path(__file__).resolve().parents[2]/'radio/src/nb4_rf_images.h'
        self.assertEqual(path.read_text(),firmware_catalog_header())

    def test_explicit_install_persists_intent_and_verifies_the_new_inventory(self):
        self.peer.can_write=True;self.peer.inventory['receiver']['write_supported']=True
        with patch.object(rf,'verify_backup',return_value={}):
            self.transfer.stage(1,self.data,self.backup)
        with self.assertRaisesRegex(wire.MaintenanceError,'confirm'):
            self.transfer.start()
        with patch.object(rf.time,'sleep',lambda _:None), patch.object(rf,'verify_backup',return_value={}):
            result=self.transfer.start(confirmed=True)
        self.assertEqual(result['phase'],5)
        self.assertEqual(self.peer.commands.count(wire.START),1)
        record=json.loads((self.root/'operation/operation.json').read_text())
        self.assertEqual(record['state'],'complete')
        self.assertEqual(record['verified_inventory']['receiver']['firmware'],0x113)
        with self.assertRaises(wire.MaintenanceError):self.transfer.start(confirmed=True)
        self.assertEqual(self.peer.commands.count(wire.START),1)

    def test_changed_connection_and_tampered_journal_do_not_send_start(self):
        self.peer.can_write=True;self.peer.inventory['receiver']['write_supported']=True
        with patch.object(rf,'verify_backup',return_value={}):self.transfer.stage(1,self.data,self.backup)
        self.peer.inventory['epoch']+=1
        with self.assertRaises(wire.MaintenanceError):self.transfer.start(confirmed=True)
        self.assertNotIn(wire.START,self.peer.commands)
        path=self.root/'operation/operation.json';data=json.loads(path.read_text());data['sha256']='0'*64;path.write_text(json.dumps(data))
        with self.assertRaises(wire.MaintenanceError):self.transfer.load()

    def test_lost_completion_is_reconciled_read_only_and_wrong_version_stays_unresolved(self):
        with patch.object(rf,'verify_backup',return_value={}): self.transfer.stage(1,self.data,self.backup)
        self.transfer.record.update(state='recovery_required',write_started=True); self.transfer.save()
        self.transfer.reconcile()
        self.assertEqual(self.transfer.record['state'],'complete')
        self.assertNotIn(wire.START,self.peer.commands); self.assertNotIn(wire.RECOVER,self.peer.commands)
        self.transfer.record['state']='recovery_required'
        bad=copy.deepcopy(self.peer.inventory); bad['receiver']['identity_words'][2]=0x107
        with patch.object(self.transfer,'inventory',return_value=bad):
            with self.assertRaisesRegex(wire.MaintenanceError,'matching hardware'): self.transfer.reconcile()
        self.assertEqual(self.transfer.record['state'],'recovery_required')
        late=dict(self.peer.status,operation=self.peer.status['operation']+1)
        with patch.object(self.transfer.client,'request',return_value=late):
            with self.assertRaises(wire.MaintenanceError): self.transfer.reconcile()

    def test_partial_preparation_is_not_replayed_or_misreported_as_a_receiver_write(self):
        with patch.object(rf,'verify_backup',return_value={}): self.transfer.stage(1,self.data,self.backup)
        pending=dict(self.peer.status,phase=1,uploaded=50,transferred=0,validated=False,cancellable=True,driving_locked=False)
        with patch.object(self.transfer.client,'request',return_value=pending),patch.object(self.transfer,'inventory',return_value=self.peer.inventory):
            self.transfer.reconcile()
        self.assertEqual(self.transfer.record['state'],'failed_before_write')
        self.assertNotIn(wire.START,self.peer.commands); self.assertNotIn(wire.RECOVER,self.peer.commands)

    def test_backup_is_reverified_immediately_before_start(self):
        self.peer.can_write=True;self.peer.inventory['receiver']['write_supported']=True
        with patch.object(rf,'verify_backup',return_value={}):
            self.transfer.stage(1,self.data,self.backup)
        with patch.object(rf,'verify_backup',side_effect=RuntimeError('backup damaged')):
            with self.assertRaises(RuntimeError): self.transfer.start(confirmed=True)
        self.assertNotIn(wire.START,self.peer.commands)
        (self.backup/'storage.json').write_text('changed')
        with patch.object(rf,'verify_backup',return_value={}):
            with self.assertRaises(wire.MaintenanceError): self.transfer.start(confirmed=True)
        self.assertNotIn(wire.START,self.peer.commands)


if __name__ == '__main__': unittest.main()
