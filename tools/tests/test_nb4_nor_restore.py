# SPDX-License-Identifier: GPL-2.0-only
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import nb4_nor_restore as restore
from test_nb4_rom_flash import full_image


class Port:
    def __init__(self,wrong=False,lost_commit=False):
        self.commands=[]; self.nonce=bytes(16); self.completed=0; self.changed=0
        self.wrong=wrong; self.lost_commit=lost_commit
        self.nor=bytearray(b'\x33'*8388608)
    def address(self,address): self.at=address
    def download(self,number,data):
        assert number==2
        self.commands.append(self.at)
        if self.at==0x92000000:
            self.nonce=data[-16:]
        elif self.at==0x92000100:
            assert struct.unpack_from('<I',data,24)[0]==self.completed
        elif self.at==0x92001000:
            self.block=data
        elif self.at==0x92000180:
            start=self.completed*4096
            self.changed+=self.nor[start:start+4096]!=self.block
            self.nor[start:start+4096]=self.block; self.completed+=1
            if self.lost_commit: raise OSError('lost after committing')
        else: raise AssertionError(self.at)
    def read(self,address,size):
        if address==0x91000000:
            return struct.pack('<8s10I',b'APXNOR1',4,0x10016419,1,0x12340000,3 if self.wrong else 2,
                               0xc84017,8388608,2097152,0,0)
        if address==0x92000200:
            return restore.STATUS.pack(7 if self.completed==2048 else 1,0,self.completed,self.changed,self.nonce)
        return bytes(self.nor[address-0x90000000:address-0x90000000+size])


class NorRestoreTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(); self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name); self.backup=self.root/'backup'; self.backup.mkdir()
        files={'internal.bin':full_image(),'nor.bin':b'\x33'*8388608}
        self.metadata=dict(format='apextx-full-unit-backup-1',raw_nor=True,target='NB4',
            identity=dict(serial='000000031234',uid=[1,0x12340000,2],mcu=0x10016419,jedec=0xc84017,
                          nor_bytes=8388608,internal_bytes=2097152),files={})
        for name,data in files.items():
            (self.backup/name).write_bytes(data)
            self.metadata['files'][name]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
        (self.backup/'backup.json').write_text(json.dumps(self.metadata))

    def test_complete_restore_is_read_back_and_identical_sectors_need_no_write(self):
        port=Port(); port.nor[:4096]=b'\x55'*4096
        result=restore.restore_connected(port,self.backup,self.root/'operation',confirmed=True)
        self.assertEqual(result['state'],'complete'); self.assertTrue(result['readback_verified'])
        self.assertEqual((result['completed'],result['changed']),(2048,1))

    def test_wrong_unit_or_changed_backup_cannot_arm(self):
        port=Port(wrong=True)
        with self.assertRaisesRegex(RuntimeError,'identity changed|different hardware'):
            restore.restore_connected(port,self.backup,self.root/'operation',confirmed=True)
        self.assertEqual(port.commands,[])
        (self.backup/'nor.bin').write_bytes(b'bad')
        with self.assertRaisesRegex(RuntimeError,'checksum'):
            restore.restore_connected(port,self.backup,self.root/'operation',confirmed=True)
        self.assertEqual(port.commands,[])

    def test_ambiguous_commit_retains_intent_and_is_not_repeated(self):
        port=Port(lost_commit=True)
        with self.assertRaises(OSError):
            restore.restore_connected(port,self.backup,self.root/'operation',confirmed=True)
        record=json.loads((self.root/'operation/restore.json').read_text())
        self.assertTrue(record['write_started']); self.assertEqual(record['state'],'recovery_required')
        self.assertEqual(port.commands.count(0x92000180),1)
        with self.assertRaises(FileExistsError):
            restore.restore_connected(Port(),self.backup,self.root/'operation',confirmed=True)

    def test_no_confirmation_and_invalid_progress_never_authorize_a_write(self):
        port=Port()
        with self.assertRaisesRegex(RuntimeError,'Confirm'):
            restore.restore_connected(port,self.backup,self.root/'operation')
        self.assertEqual(port.commands,[])
        with self.assertRaisesRegex(RuntimeError,'identity or progress'):
            restore.status(port,b'x'*16)

    def test_internal_restoration_checks_the_full_rom_uid_before_writing(self):
        import nb4_rom_flash as rom
        from types import SimpleNamespace
        def answer(command,**_):
            Path(command[-1]).write_bytes(struct.pack('<3I',1,0x12340000,2))
            return SimpleNamespace(returncode=0,stdout='')
        with patch.object(rom,'dfu_util_path',return_value='dfu-util'),patch.object(rom,'discover_rom_dfu',return_value='000000031234'),patch.object(rom,'_run',side_effect=answer),patch.object(rom,'_write_verified_image') as write:
            rom.restore_internal_backup(self.backup,lambda *_:None,'000000031234',confirmed=True)
            write.assert_called_once()
        with patch.object(rom,'dfu_util_path',return_value='dfu-util'),patch.object(rom,'discover_rom_dfu',return_value='000000031234'),patch.object(rom,'_run',return_value=SimpleNamespace(returncode=1,stdout='failed')),patch.object(rom,'_write_verified_image') as write:
            with self.assertRaisesRegex(RuntimeError,'ROM hardware identity'):
                rom.restore_internal_backup(self.backup,lambda *_:None,'000000031234',confirmed=True)
            write.assert_not_called()

if __name__=='__main__': unittest.main()
