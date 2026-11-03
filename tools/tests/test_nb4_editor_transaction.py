# SPDX-License-Identifier: GPL-2.0-only
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
import nb4_editor_files as files
import nb4_editor_transaction as txn

class EditorTransactionTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.base=pathlib.Path(self.tmp.name).resolve()
        self.root=self.base/'radio';self.root.mkdir();self.backups=self.base/'backups'
        self.file=self.root/'model.yml';self.file.write_bytes(b'original')
        self.identity={'serial':'A','target':'nb4'}
    def tearDown(self):self.tmp.cleanup()
    def test_failed_replace_keeps_original(self):
        with patch.object(files.os,'replace',side_effect=OSError('disk failure')):
            with self.assertRaises(OSError):files.write_durable(self.file,b'changed')
        self.assertEqual(self.file.read_bytes(),b'original')
    def test_unique_backups_preserve_both_versions(self):
        a=files.backup(self.file,self.backups);self.file.write_bytes(b'changed')
        b=files.backup(self.file,self.backups)
        self.assertNotEqual(a,b);self.assertEqual(a.read_bytes(),b'original');self.assertEqual(b.read_bytes(),b'changed')
    def test_commit_delete_restore_and_repeat(self):
        bg=self.root/'background.png';bg.write_bytes(b'picture')
        result=txn.commit(self.root,{'model.yml':b'changed','background.png':None},
            {'model.yml':files.digest(b'original'),'background.png':files.digest(b'picture')},self.backups,self.identity)
        self.assertEqual(self.file.read_bytes(),b'changed');self.assertFalse(bg.exists())
        txn.recover(self.root,result['backup'],self.identity,restore=True)
        self.assertEqual(self.file.read_bytes(),b'original');self.assertEqual(bg.read_bytes(),b'picture')
    def test_conflict_has_no_side_effects(self):
        with self.assertRaises(files.EditorFileError):txn.commit(self.root,{'model.yml':b'changed'},{'model.yml':'stale'},self.backups,self.identity)
        self.assertEqual(self.file.read_bytes(),b'original');self.assertFalse(self.backups.exists())
    def test_links_rejected(self):
        link=self.root/'link.yml';link.symlink_to(self.file)
        with self.assertRaises(files.EditorFileError):files.write_durable(link,b'changed')
    def test_wrong_radio_cannot_restore(self):
        r=txn.commit(self.root,{'model.yml':b'changed'},{'model.yml':files.digest(b'original')},self.backups,self.identity)
        with self.assertRaises(files.EditorFileError):txn.recover(self.root,r['backup'],{'serial':'B','target':'nb4'},True)

if __name__=='__main__':unittest.main()
