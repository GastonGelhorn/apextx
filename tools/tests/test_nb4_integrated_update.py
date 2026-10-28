# SPDX-License-Identifier: GPL-2.0-only
import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch,Mock
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import nb4_integrated_update as integrated
from nb4_backup_agent import RAM,RAM_CAPACITY
from test_nb4_rom_flash import full_image
from test_nb4_maintenance import inventory


def make_package(root):
    root.mkdir(); files={'firmware.bin':full_image(),'resources/THEMES/ApexTXDark/theme.yml':b'theme'}
    for name,kind,magic in (('backup','apextx-backup-agent-1',b'APXBKP3\0'),('nor-restore','apextx-nor-restore-agent-1',b'APXNOR1\0')):
        data=bytearray(512); struct.pack_into('<II',data,0,RAM+RAM_CAPACITY,RAM+0x101); data[64:72]=magic
        files[f'recovery/nb4-{name}-agent.bin']=bytes(data)
        files[f'recovery/{name}-agent.json']=json.dumps(dict(format=kind,sha256=hashlib.sha256(data).hexdigest())).encode()
    manifest=dict(formatVersion=3,target='NB4',hardware='nb4',release='0.1.0-alpha.6',files={})
    for name,data in files.items():
        path=root/name; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(data)
        manifest['files'][name]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    (root/'manifest.json').write_text(json.dumps(manifest))
    return root


class IntegratedTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(); self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name); self.source=make_package(self.root/'package')
        self.plan=integrated.IntegratedUpdate.create(self.source,'000000000001',1,self.root/'session')
        self.device=dict(state='normal',maintenance=True,mode='maintenance',serial='000000000001',target=1)
        self.inv=inventory()
        fields=integrated.HEADER.unpack(self.plan.package.image[-32:])
        self.inv['application']=dict(valid=True,bytes=fields[3],crc32=fields[4])
        self.inv['module'].update(product=0x10003,hardware=0x10003,firmware=0x10021,rf_library=0x30000,write_supported=False)
        self.inv['receiver'].update(product=0x12340006,identity_words=[0x104,0x104,0x107,0x111,0x301],firmware=0x111,write_supported=False)

    def read(self):
        with patch.object(integrated,'probe',return_value=self.device),patch.object(integrated,'read_inventory',return_value=copy.deepcopy(self.inv)):
            self.plan.inventory()

    def ready(self):
        self.plan.record['backup']=str(self.root/'backup')
        (self.root/'backup').mkdir(exist_ok=True); (self.root/'backup/internal.bin').write_bytes(full_image())
        self.plan.approve()
        return patch.object(integrated,'verify_backup',return_value=dict(identity=dict(serial='000000000001'),target='NB4'))

    def test_unknown_disconnected_and_changed_application_are_never_complete(self):
        self.read(); self.assertEqual(self.plan.record['steps']['application']['state'],'current')
        self.assertEqual(self.plan.record['steps']['receiver']['state'],'current')
        self.inv['connected']=False; self.inv['receiver']['valid']=False; self.inv['application']['crc32']^=1
        self.read()
        self.assertEqual(self.plan.record['steps']['receiver']['state'],'pending')
        self.assertEqual(self.plan.record['steps']['application']['state'],'ready')
        self.assertEqual(self.plan.outcome(),'partial')

    def test_plan_changes_invalidate_confirmation_and_wrong_transmitter_never_writes(self):
        self.read(); self.plan.approve()
        self.plan.select(dict(self.plan.record['selected'],audio=False))
        self.assertFalse(self.plan.record['approved'])
        with self.ready(),patch.object(integrated,'probe',return_value=dict(self.device,serial='another')),patch.object(integrated,'run_update') as write:
            with self.assertRaises(RuntimeError): self.plan.application(lambda *_:None)
            write.assert_not_called()

    def test_dependencies_and_recovery_stop_following_phases(self):
        self.read()
        self.plan.record['steps']['module']['state']='recovery_required'
        self.assertEqual(self.plan.outcome(),'recovery_required','Recovery takes priority over a pending bootloader or other failure.')
        with self.ready(),patch.object(integrated,'probe',return_value=self.device),patch.object(integrated,'connection') as port:
            with self.assertRaisesRegex(RuntimeError,'module'): self.plan.rf('receiver','install',lambda *_:None)
            with self.assertRaisesRegex(RuntimeError,'Recover'): self.plan.prepare_storage()
            port.assert_not_called()

    def test_loading_an_interrupted_application_does_not_replay_it(self):
        self.plan.record['steps']['application']['state']='running'; self.plan.save()
        with patch.object(integrated,'run_update') as write:
            reopened=integrated.IntegratedUpdate.load(self.plan.root)
            self.assertEqual(reopened.record['steps']['application']['state'],'recovery_required')
            write.assert_not_called()

    def test_current_rf_is_not_reinstalled_and_bench_reinstall_still_needs_qualification(self):
        from test_nb4_rf_transfer import Radio
        from nb4_maintenance import START,PREPARE
        peer=Radio(); peer.inventory=copy.deepcopy(self.inv); self.read()
        manager=Mock(); manager.__enter__=Mock(return_value=peer); manager.__exit__=Mock(return_value=False)
        with self.ready(),patch.object(integrated,'probe',return_value=self.device),patch.object(integrated,'connection',return_value=manager):
            self.plan.rf('receiver','install',lambda *_:None)
            with self.assertRaisesRegex(RuntimeError,'not qualified'):
                self.plan.rf('receiver','install',lambda *_:None,reinstall=True)
        self.assertNotIn(START,peer.commands); self.assertNotIn(PREPARE,peer.commands)

    def test_package_is_copied_offline_and_tampering_or_wrong_hardware_is_rejected(self):
        (self.source/'firmware.bin').write_bytes(b'changed')
        self.assertEqual(len(integrated.IntegratedUpdate.load(self.plan.root).package.image),2097152)
        with self.assertRaises(RuntimeError): integrated.Package(self.source)
        with self.assertRaises(RuntimeError): integrated.IntegratedUpdate.create(self.plan.package.root,'000000000001',2,self.root/'wrong')
        (self.plan.package.root/'firmware.bin').write_bytes(b'changed')
        with self.assertRaises(RuntimeError): integrated.IntegratedUpdate.load(self.plan.root)

    def test_bootloader_is_read_in_update_mode_and_incomplete_session_blocks_app_restart(self):
        from nb4_flasher_session import FlasherSession
        with patch.object(integrated,'probe',return_value=dict(self.device,state='ready',bootloader=1)),patch.object(integrated,'read_inventory') as serial:
            result=self.plan.inventory()
            self.assertEqual(result['bootloader']['state'],'current'); serial.assert_not_called()
        self.plan.record['steps']['application']['state']='running'; self.plan.save()
        with patch.object(Path,'home',return_value=self.root):
            previous=FlasherSession(); previous.integrated=self.plan; previous.remember_components()
            reopened=FlasherSession()
            with patch.object(integrated,'probe',side_effect=AssertionError('resume must not access USB')):
                reopened.resume_components()
            self.assertEqual(reopened.integrated_view['outcome'],'recovery_required')
            with self.assertRaises(RuntimeError): reopened.reserve_app_update()

    def test_archive_paths_links_and_multiple_packages_are_rejected(self):
        for name,mode in (('../outside',0o100644),('link',0o120777)):
            data=io.BytesIO()
            with zipfile.ZipFile(data,'w') as archive:
                entry=zipfile.ZipInfo(name); entry.external_attr=mode<<16; archive.writestr(entry,b'bad')
            with self.subTest(name=name),self.assertRaises(RuntimeError):
                integrated.unpack_package(data.getvalue(),self.root/('out'+str(mode)))
        self.assertFalse((self.root/'outside').exists())

if __name__=='__main__': unittest.main()
