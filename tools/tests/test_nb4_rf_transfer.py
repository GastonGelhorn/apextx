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
        self.installed = 0x113  # receiver version reported once an operation completes
        self.assets, self.stage = {}, None  # verified card files; the open stage file
        self.corrupt_asset = False
        self.reboot_requested = False

    def write(self, frame):
        cmd, request, payload = wire.Decoder().feed(frame)[0]
        self.commands.append(cmd)
        if cmd == wire.CAPABILITIES:
            result = dict(protocol=1, inventory=True, file_transfer=True, assets=True, reboot_update=True,
                          rf_write=self.can_write, rf_recover=self.can_write)
        elif cmd == wire.ASSET_BEGIN:
            index = payload[0]; size = rf.IMAGES[index]['bytes']
            if index in self.assets:
                result = dict(asset=index, verified=True, uploaded=size, bytes=size)
            else:
                self.stage = (index, bytearray()); result = dict(asset=index, verified=False, uploaded=0, bytes=size)
        elif cmd == wire.ASSET_DATA:
            index, offset = struct.unpack_from('<BI', payload); block = payload[5:]
            assert self.stage and self.stage[0] == index and offset == len(self.stage[1]) and 1 <= len(block) <= 1019
            self.stage[1].extend(block); size = rf.IMAGES[index]['bytes']
            if len(self.stage[1]) < size:
                result = dict(asset=index, verified=False, uploaded=len(self.stage[1]), bytes=size)
            else:
                data = bytes(self.stage[1]); self.stage = None
                if self.corrupt_asset or hashlib.sha256(data).hexdigest() != rf.IMAGES[index]['sha256']:
                    result = dict(error='asset_verification_failed')
                else:
                    self.assets[index] = data; result = dict(asset=index, verified=True, uploaded=size, bytes=size)
        elif cmd == wire.REBOOT_UPDATE:
            assert not payload
            self.reboot_requested = True; result = dict(reboot_update=True)
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
            self.inventory['receiver']['firmware']=self.installed
            self.inventory['receiver']['identity_words'][3]=self.installed
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

    def test_provisional_fgr8b_rule_matches_the_product_on_nb4plus_only(self):
        self.catalog.stop(); self.addCleanup(self.catalog.start)  # the full catalog, index 8 included
        receiver = self.peer.inventory['receiver']
        receiver.update(product=0x10107, firmware=0x114, identity_words=[0x205, 0x206, 0x109, 0x114, 0x302])
        self.peer.inventory['target'] = 'nb4plus'
        self.assertEqual(rf.plan(self.peer.inventory, 8)['state'], 'available')
        receiver['firmware'] = receiver['identity_words'][3] = 0x117
        self.assertEqual(rf.plan(self.peer.inventory, 8)['state'], 'current')
        receiver['product'] = 0x10108
        self.assertEqual(rf.plan(self.peer.inventory, 8)['state'], 'incompatible')
        receiver['product'] = 0x10107
        self.peer.inventory['target'] = 'nb4'
        self.assertEqual(rf.plan(self.peer.inventory, 8)['state'], 'incompatible')
        self.assertEqual(rf.plan(self.peer.inventory, 3)['state'], 'incompatible')
        # The wildcard never widens the pinned FGr4 rules.
        receiver.update(product=0x12340004, firmware=0x112, identity_words=[0x205, 0x104, 0x103, 0x112, 0x301])
        self.assertEqual(rf.plan(self.peer.inventory, 1)['state'], 'incompatible')

    def test_receiver_update_requires_a_live_two_way_link_before_preparation(self):
        for connected, two_way in ((False, True), (True, False), (False, False)):
            with self.subTest(connected=connected, two_way=two_way):
                self.peer.inventory.update(connected=connected, two_way=two_way)
                self.peer.inventory['receiver']['valid']=False
                result=rf.plan(self.peer.inventory,1)
                self.assertEqual(result['state'],'unknown'); self.assertFalse(result['write_supported'])
                self.assertIn('Bind and connect',result['reason'])
                with self.assertRaisesRegex(wire.MaintenanceError, 'Bind and connect'):
                    self.transfer.stage(1,self.data,self.backup)
                self.assertNotIn(wire.PREPARE,self.peer.commands)

    def test_stale_receiver_identity_cannot_bypass_the_live_link_check(self):
        self.peer.inventory['connected']=False
        self.assertTrue(self.peer.inventory['receiver']['valid'])
        with self.assertRaises(wire.MaintenanceError):
            self.transfer.stage(1,self.data,self.backup)
        self.assertNotIn(wire.PREPARE,self.peer.commands)

    def test_receiver_disconnect_after_staging_cannot_start_a_write(self):
        self.peer.can_write=True; self.peer.inventory['receiver']['write_supported']=True
        with patch.object(rf,'verify_backup',return_value={}):
            self.transfer.stage(1,self.data,self.backup)
        self.peer.inventory['connected']=False; self.peer.inventory['receiver']['valid']=False
        with self.assertRaisesRegex(wire.MaintenanceError, 'changed'):
            self.transfer.start(confirmed=True)
        self.assertNotIn(wire.START,self.peer.commands)
        self.assertFalse(self.transfer.load()['write_started'])

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

    def test_completion_waits_for_restarted_receiver_inventory_without_rewriting(self):
        self.peer.can_write=True; self.peer.inventory['receiver']['write_supported']=True
        with patch.object(rf,'verify_backup',return_value={}):
            self.transfer.stage(1,self.data,self.backup)
        before=copy.deepcopy(self.peer.inventory)
        pending=copy.deepcopy(before); pending['connected']=False; pending['receiver']['valid']=False
        after=copy.deepcopy(before)
        after['receiver'].update(firmware=0x113,identity_words=[0x104,0x104,0x103,0x113,0x301])
        with patch.object(self.transfer,'inventory',side_effect=(before,pending,after)), \
             patch.object(rf,'verify_backup',return_value={}), \
             patch.object(rf.time,'sleep',lambda _:None):
            self.assertEqual(self.transfer.start(confirmed=True)['phase'],5)
        self.assertEqual(self.transfer.load()['state'],'complete')
        self.assertEqual(self.peer.commands.count(wire.START),1)
        self.assertNotIn(wire.RECOVER,self.peer.commands)

    def test_completion_rejects_matching_version_on_a_different_hardware_revision(self):
        self.peer.can_write=True; self.peer.inventory['receiver']['write_supported']=True
        with patch.object(rf,'verify_backup',return_value={}):
            self.transfer.stage(1,self.data,self.backup)
        before=copy.deepcopy(self.peer.inventory); wrong=copy.deepcopy(before)
        wrong['receiver'].update(firmware=0x113,identity_words=[0x104,0x104,0x107,0x113,0x301])
        with patch.object(self.transfer,'inventory',side_effect=(before,wrong)), \
             patch.object(rf,'verify_backup',return_value={}), \
             patch.object(rf.time,'sleep',lambda _:None):
            with self.assertRaisesRegex(wire.MaintenanceError,'could not be verified'):
                self.transfer.start(confirmed=True)
        self.assertNotEqual(self.transfer.load()['state'],'complete')
        self.assertEqual(self.peer.commands.count(wire.START),1)
        self.assertNotIn(wire.RECOVER,self.peer.commands)

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

    def test_recovery_waits_for_image_validation_without_repeating_the_command(self):
        with patch.object(rf,'verify_backup',return_value={}):
            self.transfer.stage(1,self.data,self.backup)
        pending=dict(self.peer.status,phase=7,driving_locked=True,cancellable=False)
        running=dict(pending,phase=2)
        complete=dict(pending,phase=5,driving_locked=False,transferred=len(self.data))
        statuses=iter((pending,pending,pending,running,complete))
        commands=[]
        def request(command,payload=b''):
            commands.append(command)
            if command==wire.CAPABILITIES:return {'rf_recover':True}
            if command==wire.RECOVER:return pending
            if command==wire.STATUS:return next(statuses)
            raise AssertionError(command)
        after=copy.deepcopy(self.peer.inventory)
        after['receiver'].update(firmware=0x113,identity_words=[0x104,0x104,0x103,0x113,0x301])
        with patch.object(self.transfer.client,'request',request), \
             patch.object(self.transfer,'inventory',side_effect=(self.peer.inventory,after)), \
             patch.object(rf,'verify_backup',return_value={}), \
             patch.object(rf.time,'sleep',lambda _:None):
            self.assertEqual(self.transfer.recover(confirmed=True)['phase'],5)
        self.assertEqual(commands.count(wire.RECOVER),1)
        self.assertNotIn(wire.START,commands)
        self.assertEqual(self.transfer.load()['state'],'complete')

    def test_recovery_that_never_starts_stays_unresolved_without_a_retry(self):
        with patch.object(rf,'verify_backup',return_value={}):
            self.transfer.stage(1,self.data,self.backup)
        pending=dict(self.peer.status,phase=7,driving_locked=True,cancellable=False)
        commands=[]
        def request(command,payload=b''):
            commands.append(command)
            return {'rf_recover':True} if command==wire.CAPABILITIES else pending
        ticks=iter(range(100))
        with patch.object(self.transfer.client,'request',request), \
             patch.object(self.transfer,'inventory',return_value=self.peer.inventory), \
             patch.object(rf,'verify_backup',return_value={}), \
             patch.object(rf.time,'sleep',lambda _:None):
            with self.assertRaisesRegex(wire.MaintenanceError,'needs recovery'):
                self.transfer.recover(confirmed=True,clock=lambda:next(ticks))
        self.assertEqual(commands.count(wire.RECOVER),1)
        self.assertNotIn(wire.START,commands)
        self.assertLess(commands.count(wire.STATUS),10)
        self.assertEqual(self.transfer.load()['state'],'recovery_required')


class AssetTests(unittest.TestCase):
    def setUp(self):
        self.data = {0: b'm' * 700, 1: b'r' * 1200, 2: b's' * 3001}
        images = tuple(dict(rf.IMAGES[i], bytes=len(d), sha256=hashlib.sha256(d).hexdigest()) for i, d in self.data.items())
        catalog = patch.object(rf, 'IMAGES', images + rf.IMAGES[3:]); catalog.start(); self.addCleanup(catalog.stop)
        self.files = {rf.image_name(images[i]): d for i, d in self.data.items()}
        self.peer = Radio(); self.client = rf.AssetClient(self.peer)

    def test_publish_skips_verified_images_and_streams_the_rest_sequentially(self):
        self.peer.assets[0] = self.data[0]
        result = rf.publish_assets(self.client, 'nb4', self.files)
        self.assertEqual([(r['image'], r['state']) for r in result], [(0, 'present'), (1, 'uploaded'), (2, 'uploaded')])
        self.assertEqual(self.peer.assets, self.data)
        self.assertEqual(self.peer.commands.count(wire.ASSET_BEGIN), 3)
        self.assertEqual(self.peer.commands.count(wire.ASSET_DATA), 3 + 6)
        self.assertFalse({wire.PREPARE, wire.UPLOAD, wire.START, wire.REBOOT_UPDATE} & set(self.peer.commands))

    def test_verification_failure_is_surfaced_and_nothing_is_kept(self):
        self.peer.corrupt_asset = True
        with self.assertRaisesRegex(wire.MaintenanceError, 'RF module.*asset_verification_failed'):
            rf.publish_assets(self.client, 'nb4', self.files)
        self.assertEqual(self.peer.assets, {})

    def test_tampered_or_missing_catalog_bytes_never_reach_the_radio(self):
        files = dict(self.files); files[rf.image_name(rf.IMAGES[0])] = b'x' * 700
        with self.assertRaisesRegex(wire.MaintenanceError, 'verification failed'):
            rf.publish_assets(self.client, 'nb4', files)
        with self.assertRaises(wire.MaintenanceError):
            rf.publish_assets(self.client, 'nb4', {})
        self.assertNotIn(wire.ASSET_DATA, self.peer.commands)
        capabilities = dict(protocol=1, inventory=True)
        with patch.object(self.client, 'request', return_value=capabilities):
            with self.assertRaisesRegex(wire.MaintenanceError, 'does not accept'):
                rf.publish_assets(self.client, 'nb4', self.files)

    def test_foreign_or_non_sequential_progress_stops_the_upload(self):
        replies = {wire.CAPABILITIES: dict(protocol=1, assets=True),
                   wire.ASSET_BEGIN: dict(asset=0, verified=False, uploaded=0, bytes=700),
                   wire.ASSET_DATA: dict(asset=0, verified=False, uploaded=7, bytes=700)}
        with patch.object(self.client, 'request', side_effect=lambda cmd, payload=b'': replies[cmd]) as request:
            with self.assertRaisesRegex(wire.MaintenanceError, 'does not match'):
                rf.publish_assets(self.client, 'nb4', self.files)
        self.assertEqual([c.args[0] for c in request.call_args_list].count(wire.ASSET_DATA), 1)
        replies[wire.ASSET_BEGIN] = dict(asset=1, verified=False, uploaded=0, bytes=700)
        with patch.object(self.client, 'request', side_effect=lambda cmd, payload=b'': replies[cmd]):
            with self.assertRaisesRegex(wire.MaintenanceError, 'Invalid RF asset'):
                rf.publish_assets(self.client, 'nb4', self.files)

    def test_reboot_requires_the_capability_and_an_explicit_confirmation(self):
        rf.reboot_into_update(self.client)
        self.assertTrue(self.peer.reboot_requested)
        self.assertEqual(self.peer.commands, [wire.CAPABILITIES, wire.REBOOT_UPDATE])
        self.peer.reboot_requested = False
        with patch.object(self.client, 'request', return_value=dict(protocol=1, inventory=True)):
            with self.assertRaises(wire.MaintenanceError): rf.reboot_into_update(self.client)
        with patch.object(self.client, 'request', side_effect=[dict(reboot_update=True), dict(reboot_update=False)]):
            with self.assertRaisesRegex(wire.MaintenanceError, 'did not confirm'): rf.reboot_into_update(self.client)
        self.assertFalse(self.peer.reboot_requested)

    def test_asset_client_cannot_prepare_start_or_recover_an_rf_write(self):
        for command in (wire.PREPARE, wire.UPLOAD, wire.START, wire.CANCEL, wire.RECOVER, wire.STORAGE_SESSION):
            with self.assertRaises(wire.MaintenanceError): self.client.request(command, b'')
        self.assertEqual(self.peer.commands, [])

    def test_installable_images_follow_the_production_rules_of_both_targets(self):
        self.assertEqual(rf.installable_images('nb4'), [0, 1, 2])
        self.assertEqual(rf.installable_images('nb4plus'), [0, 1, 2, 8])
        with self.assertRaises(wire.MaintenanceError): rf.installable_images('nb4pro')
        self.assertLessEqual(rf.ASSET_CHUNK + 5, wire.MAX_PAYLOAD)


if __name__ == '__main__': unittest.main()
