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
    def test_app_update_cannot_restart_with_pending_editor_drafts(self):
        from nb4_flasher_session import FlasherSession
        from nb4_usb_update import UpdateError
        session=FlasherSession(client=object());session.editor_dirty=True
        with self.assertRaises(UpdateError):session.reserve_app_update()
        self.assertFalse(session.app_updating)
        session.editor_dirty=False;session.reserve_app_update();self.assertTrue(session.app_updating)
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

    def test_discovered_themes_folder_without_radio_identity_is_not_an_offline_destination(self):
        mounted=pathlib.Path(self.tmp.name).resolve()/'other-volume'
        (mounted/'THEMES').mkdir(parents=True)
        editor.EXTRA_ROOTS.append(mounted)
        with self.assertRaisesRegex(ValueError,'Update the radio firmware'):
            editor.save_theme(str(mounted),[dict(name='THEMES/Demo/theme.yml',
                data=base64.b64encode(b'summary:\n  name: Demo\n').decode())])
        self.assertFalse((mounted/'THEMES/Demo').exists())

    def test_explicit_computer_library_remains_writable_offline(self):
        home=pathlib.Path(self.tmp.name).resolve()/'home'
        with patch.object(pathlib.Path,'home',return_value=home):
            library=home/'ApexTX-Themes'
            result=editor.save_theme(str(library),[dict(name='THEMES/Demo/theme.yml',
                data=base64.b64encode(b'summary:\n  name: Demo\n').decode())])
        self.assertTrue(result['ok'])
        self.assertTrue((library/'THEMES/Demo/theme.yml').is_file())

    def test_new_theme_refuses_existing_resource_folder(self):
        folder=self.root/'THEMES/Demo';folder.mkdir()
        (folder/'custom-resource.txt').write_text('keep this')
        with self.assertRaisesRegex(ValueError,'folder already exists'):
            editor.save_theme(str(self.root),[dict(name='THEMES/Demo/theme.yml',
                data=base64.b64encode(b'summary:\n  name: Demo\n').decode())])
        self.assertFalse((folder/'theme.yml').exists())
        self.assertEqual((folder/'custom-resource.txt').read_text(),'keep this')

    def test_load_returns_each_orientation_background_without_conflating_them(self):
        folder=self.root/'THEMES/Demo';folder.mkdir()
        (folder/'theme.yml').write_text('summary:\n  name: Demo\n')
        (folder/'background.png').write_bytes(b'portrait bytes')
        (folder/'background_480x320.png').write_bytes(b'landscape bytes')
        result=editor.read_theme(str(self.root),'Demo')
        self.assertEqual(base64.b64decode(result['background']),b'portrait bytes')
        self.assertEqual(base64.b64decode(result['backgroundLandscape']),b'landscape bytes')
        self.assertEqual(result['hashes']['THEMES/Demo/background_480x320.png'],files.digest(b'landscape bytes'))

    def test_oversized_background_is_not_silently_read_as_absent(self):
        folder=self.root/'THEMES/Demo';folder.mkdir()
        (folder/'theme.yml').write_text('summary:\n  name: Demo\n')
        with (folder/'background.png').open('wb') as stream:
            stream.truncate(editor.MAX_THEME_BYTES+1)
        with self.assertRaisesRegex(ValueError,'too large'):
            editor.read_theme(str(self.root),'Demo')

    def test_changed_widget_options_require_their_canonical_yaml_value(self):
        self.caps['catalog']=[{'id':'NB4ThrBar','options':[{'type':'Integer','min':0,'max':10}]}]
        self.marker.write_text(json.dumps(self.caps))
        path=self.root/'MODELS/model1.yml'
        path.write_text('nb4ScreenVersion: 1\nscreenData:\n  0:\n    LayoutId: Layout1x1\n    layoutData:\n      zones:\n        0:\n          widgetName: "NB4ThrBar"\n')
        model=files.read_model(self.root,'model1.yml')
        option={'type':'Signed','value':{'signedValue':'5'}}
        model['screenData']['0']['layoutData']['zones']['0']['widgetData']={'options':{'0':option}}
        editor.validate_model_capabilities(self.root,model)
        for value in ({},{'unsignedValue':'5'},{'signedValue':'5','other':'6'}):
            option['value']=value
            with self.subTest(value=value),self.assertRaisesRegex(ValueError,'option value'):
                editor.validate_model_capabilities(self.root,model)

if __name__=='__main__':unittest.main()
