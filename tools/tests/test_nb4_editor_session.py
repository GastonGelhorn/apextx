# SPDX-License-Identifier: GPL-2.0-only
import base64
import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import nb4_editor_files as files
spec=importlib.util.spec_from_file_location('editor_session_test',ROOT/'tools/apextx-theme-editor.py')
editor=importlib.util.module_from_spec(spec);spec.loader.exec_module(editor)

class EditorSessionTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=pathlib.Path(self.tmp.name).resolve()/'radio'
        for name in ('RADIO','MODELS','THEMES'): (self.root/name).mkdir(parents=True,exist_ok=True)
        (self.root/'RADIO/radio.yml').write_bytes(b'selectedTheme: "ApexTX Dark"\nnb4Orientation: 0\n')
        self.caps=dict(format='apextx-editor-1',protocol=1,target='nb4',serial='A',firmware='abc',widgets=['NB4ThrBar'],layouts=['Layout1x1'],sources=['NONE'])
        self.marker=self.root/'APEXTX-EDITOR.json';self.marker.write_text(json.dumps(self.caps))
        self.device=dict(mode='storage',target=1,serial='A',revision='abc')
        self.patch=patch.object(editor,'probe',return_value=self.device);self.patch.start()
        self.roots=list(editor.EXTRA_ROOTS);editor.EXTRA_ROOTS[:]=[self.root]
        self.backup=patch.object(editor,'BACKUP_ROOT',pathlib.Path(self.tmp.name)/'backup');self.backup.start()
    def tearDown(self):
        self.patch.stop();self.backup.stop();editor.EXTRA_ROOTS[:]=self.roots;self.tmp.cleanup()
    def test_legacy_and_other_radio_are_read_only(self):
        self.marker.unlink()
        with self.assertRaises(ValueError):editor.writable_identity(str(self.root))
        self.marker.write_text(json.dumps(self.caps));self.device['serial']='B'
        with self.assertRaises(ValueError):editor.writable_identity(str(self.root))
    def test_stale_firmware_descriptor_cannot_authorize_a_save(self):
        self.device['revision']='other'
        with self.assertRaises(ValueError):editor.writable_identity(str(self.root))
    def test_invalid_theme_set_writes_nothing(self):
        with self.assertRaises(ValueError):editor.save_theme(str(self.root),[
            {'name':'THEMES/Demo/theme.yml','data':base64.b64encode(b'summary:\n name: Demo\n').decode()},
            {'name':'../outside','data':''}])
        self.assertFalse((self.root/'THEMES/Demo').exists())
    def test_stale_radio_hash_prevents_theme_commit_too(self):
        with self.assertRaises(files.EditorFileError):editor.save_changes(dict(target=str(self.root),
            files=[dict(name='THEMES/Demo/theme.yml',data=base64.b64encode(b'summary:\n name: Demo\n').decode())],
            settings={'nb4Orientation':1},radioHash='stale'))
        self.assertFalse((self.root/'THEMES/Demo').exists())
        self.assertIn(b'nb4Orientation: 0',(self.root/'RADIO/radio.yml').read_bytes())
    def test_removing_background_replaces_the_theme_set(self):
        folder=self.root/'THEMES/Demo';folder.mkdir()
        (folder/'theme.yml').write_bytes(b'old');(folder/'background.png').write_bytes(b'image')
        hashes={f'THEMES/Demo/{p.name}':files.digest(p.read_bytes()) for p in folder.iterdir()}
        result=editor.save_theme(str(self.root),[dict(name='THEMES/Demo/theme.yml',data=base64.b64encode(b'summary:\n  name: Demo\n').decode())],hashes)
        self.assertFalse((folder/'background.png').exists());self.assertTrue(pathlib.Path(result['backup']).is_dir())

if __name__=='__main__':unittest.main()
