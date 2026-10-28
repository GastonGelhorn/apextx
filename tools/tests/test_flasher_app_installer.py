# SPDX-License-Identifier: GPL-2.0-only
import json
import pathlib
import plistlib
import stat
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import Mock, patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import flasher_app_installer as installer
from flasher_app_update import AppUpdateError, atomic_json, file_digest


class ArchiveValidation(unittest.TestCase):
    def archive(self, folder, entries):
        archive = folder / 'app.zip'
        with zipfile.ZipFile(archive,'w') as output:
            for name, content in entries:
                if isinstance(name, str):
                    # Preserve malicious raw ZIP names even when the host's
                    # ZipInfo constructor would normalize Windows separators.
                    entry = zipfile.ZipInfo(name)
                    entry.filename = name
                    name = entry
                output.writestr(name, content)
        return archive

    def test_traversal_wrong_bundle_duplicate_and_special_files_are_rejected(self):
        entries = [('../outside', b'bad'), ('Other.app/a', b'bad'), ('ApexTX-Flasher.app/../../outside', b'bad'),
                   ('ApexTX-Flasher.app\\evil', b'bad')]
        for entry in entries:
            with tempfile.TemporaryDirectory() as directory:
                folder = pathlib.Path(directory)
                archive = self.archive(folder, [entry])
                with self.assertRaises(AppUpdateError): installer.extract_bundle(archive,folder/'extract')
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            archive = self.archive(folder,[('ApexTX-Flasher.app/a',b'1'),('ApexTX-Flasher.app/A',b'2')])
            with self.assertRaises(AppUpdateError): installer.extract_bundle(archive,folder/'extract')

    def test_external_and_absolute_symlinks_are_rejected(self):
        for target in ('../../outside','/tmp/outside'):
            with tempfile.TemporaryDirectory() as directory:
                folder = pathlib.Path(directory)
                info = zipfile.ZipInfo('ApexTX-Flasher.app/link')
                info.create_system = 3
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                archive = self.archive(folder,[(info,target)])
                with self.assertRaises(AppUpdateError): installer.extract_bundle(archive,folder/'extract')

    def test_bundle_identifier_and_codesign_are_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            archive = self.archive(folder,[('ApexTX-Flasher.app/Contents/Info.plist',
                plistlib.dumps(dict(CFBundleIdentifier='org.apextx.flasher'))),
                ('ApexTX-Flasher.app/'+installer.EXECUTABLE,b'code')])
            with patch.object(installer.subprocess,'run') as run:
                app = installer.extract_bundle(archive,folder/'extract')
                self.assertTrue(installer.executable(app).is_file())
                self.assertIn('--strict',run.call_args.args[0])

    def test_file_below_symlink_cannot_write_outside_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            info = zipfile.ZipInfo('ApexTX-Flasher.app/link')
            info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive = self.archive(folder,[(info,'../../outside'),('ApexTX-Flasher.app/link/payload',b'bad')])
            with self.assertRaises(AppUpdateError): installer.extract_bundle(archive,folder/'extract')
            self.assertFalse((folder/'outside').exists())


class Replacement(unittest.TestCase):
    def test_environment_does_not_reuse_pyinstaller_parent_runtime(self):
        with patch.dict('os.environ',{'_PYI_APPLICATION_HOME_DIR':'old','PYTHONPATH':'bad','DYLD_LIBRARY_PATH':'bad'}):
            env = installer.clean_environment()
            self.assertNotIn('_PYI_APPLICATION_HOME_DIR',env)
            self.assertNotIn('PYTHONPATH',env)
            self.assertNotIn('DYLD_LIBRARY_PATH',env)
            self.assertEqual(env['PYINSTALLER_RESET_ENVIRONMENT'],'1')

    @unittest.skipUnless(sys.platform in ('win32','darwin'), 'Platform replacement primitive')
    def test_real_same_volume_replacement_preserves_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            old,new = folder/'old',folder/'new'
            old.write_bytes(b'working-old')
            new.write_bytes(b'verified-new')
            installer.swap(old,new)
            self.assertEqual(old.read_bytes(),b'verified-new')
            if sys.platform=='darwin':
                self.assertEqual(new.read_bytes(),b'working-old')
                installer.swap(old,new)
                self.assertEqual(old.read_bytes(),b'working-old')

    def test_rollback_restores_verified_previous_copy_without_killing_processes(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            destination,payload,backup = folder/'App.exe',folder/'new.exe',folder/'Previous.exe'
            destination.write_bytes(b'new')
            backup.write_bytes(b'old')
            doc = dict(destination=str(destination),current=str(destination),payload=str(payload),
                backup=str(backup),new_digest=file_digest(destination),version='1.5.0',nonce='0'*64)
            transaction = folder/'transaction.json'
            def replace(old,new):
                old.write_bytes(new.read_bytes())
            with patch.object(installer,'swap',side_effect=replace), patch.object(installer.sys,'platform','win32'), \
                    patch.object(installer,'data_directory',return_value=folder), patch.object(installer.subprocess,'Popen') as launch:
                installer.rollback(transaction,doc,'startup failed')
            self.assertEqual(destination.read_bytes(),b'old')
            self.assertEqual(json.loads(transaction.read_bytes())['phase'],'rolled-back')
            launch.assert_called_once()

    def test_relocation_rollback_never_changes_original(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            current,destination = folder/'old.exe',folder/'user.exe'
            current.write_bytes(b'working')
            destination.write_bytes(b'failed')
            doc = dict(destination=str(destination),current=str(current),payload=str(folder/'staged.exe'),
                backup=str(folder/'Previous.exe'),new_digest=file_digest(destination),version='1.5.0',nonce='0'*64)
            with patch.object(installer,'data_directory',return_value=folder), patch.object(installer.subprocess,'Popen'):
                installer.rollback(folder/'transaction.json',doc,'failed')
            self.assertEqual(current.read_bytes(),b'working')

    def test_invalid_transaction_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).resolve()
            folder = root/'update-test'
            folder.mkdir()
            current = root/'App.exe'
            doc = dict(schema=1,platform=sys.platform,current=str(current),destination=str(current),
                payload=str(root/'arbitrary.exe'),backup=str(root/'backup.exe'),runner=str(folder/'runner.exe'),nonce='0'*64)
            path = folder/'transaction.json'
            atomic_json(path,doc)
            with patch.object(installer,'data_directory',return_value=root):
                with self.assertRaises(AppUpdateError): installer.read_transaction(path)
            with self.assertRaises(AppUpdateError): installer.read_transaction(path)

    def test_startup_nonce_version_and_destination_are_required_before_health(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)/'transaction.json'
            doc = dict(nonce='a'*64,phase='starting',version='1.5.0',destination='/App.exe')
            with patch.object(installer,'read_transaction',return_value=doc),patch.object(installer,'installed_app',return_value=pathlib.Path('/App.exe')):
                for nonce,version in (('b'*64,'1.5.0'),('a'*64,'1.4.0')):
                    with self.assertRaises(AppUpdateError): installer.startup_handshake(path,nonce,version)
                callback = installer.startup_handshake(path,'a'*64,'1.5.0')
                doc['phase']='confirmed'
                atomic_json(path,doc)
                callback()
                self.assertEqual(json.loads((path.parent/'healthy.json').read_bytes())['nonce'],'a'*64)

    def test_live_helper_prevents_ordinary_app_or_cli_startup(self):
        from flasher_app_update import InstanceLease
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            atomic_json(folder/'active-update.json',dict(helper=12345,nonce='a'*64))
            with patch.object(installer,'parent_alive',return_value=True):
                with self.assertRaises(AppUpdateError): InstanceLease(folder)
                InstanceLease(folder,update_nonce='a'*64).close()

class HelperTransaction(unittest.TestCase):
    def transaction(self, root):
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        import base64
        from tools.tests.test_flasher_app_update import signed_release
        doc, _, _, _, _ = signed_release()
        folder = root/'update-fixture'
        folder.mkdir()
        stage = root/'.apextx-update-fixture'
        stage.mkdir()
        current,payload,backup = root/'App.exe',stage/'App.exe',stage/'Previous.exe'
        current.write_bytes(b'old-app')
        payload.write_bytes(b'new-app')
        backup.write_bytes(b'old-app')
        name = 'ApexTX-Flasher-1.5.0-windows-x86_64.exe'
        doc['packages'] = {'windows-x86_64':dict(name=name,size=7,minimum_os='10.0.17763',sha256=file_digest(payload))}
        raw = json.dumps(doc).encode()
        key = Ed25519PrivateKey.generate()
        (folder/installer.MANIFEST).write_bytes(raw)
        (folder/installer.SIGNATURE).write_bytes(key.sign(raw))
        (folder/name).write_bytes(b'new-app')
        runner = folder/'Updater.exe'
        runner.write_bytes(b'old-app')
        journal = dict(schema=1,phase='prepared',nonce='0'*64,parent=12345,current=str(current),
            destination=str(current),payload=str(payload),backup=str(backup),runner=str(runner),
            version='1.5.0',revision='a'*40,platform='win32',old_digest=file_digest(current),new_digest=file_digest(payload))
        path = folder/'transaction.json'
        atomic_json(path,journal)
        keys = {'fixture':base64.b64encode(key.public_key().public_bytes_raw()).decode()}
        return path, current, keys

    def test_helper_confirms_success_and_rolls_back_early_startup_failure(self):
        import flasher_app_update as app
        for succeeds in (True,False):
            with self.subTest(succeeds=succeeds), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory).resolve()
                path,current,keys = self.transaction(root)
                launched=[]
                def launch(*args,**kwargs):
                    process=Mock()
                    process.poll.return_value=None if succeeds else 1
                    launched.append(process)
                    if succeeds: atomic_json(path.parent/'healthy.json',dict(nonce='0'*64))
                    return process
                def replace(old,new):
                    new.replace(old)
                with patch.object(installer.sys,'platform','win32'), \
                        patch.object(installer,'data_directory',return_value=root), \
                        patch.object(installer,'target_platform',return_value='windows-x86_64'), \
                        patch.object(installer,'InstanceLease'), \
                        patch.object(installer,'parent_alive',return_value=False), \
                        patch.object(app,'trusted_keys',return_value=keys), \
                        patch.object(installer,'swap',side_effect=replace), \
                        patch.object(installer.subprocess,'Popen',side_effect=launch):
                    installer.apply_update(path,'0'*64)
                self.assertEqual(current.read_bytes(),b'new-app' if succeeds else b'old-app')
                self.assertEqual(json.loads(path.read_bytes())['phase'],'confirmed' if succeeds else 'rolled-back')
                self.assertFalse((root/'active-update.json').exists())
                for process in launched:
                    process.kill.assert_not_called()
                    process.terminate.assert_not_called()

    def test_tampered_staged_app_is_rejected_before_helper_ready_or_parent_exit(self):
        import flasher_app_update as app
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory).resolve()
            path,current,keys=self.transaction(root)
            doc=json.loads(path.read_bytes())
            pathlib.Path(doc['payload']).write_bytes(b'modified')
            with patch.object(installer.sys,'platform','win32'),patch.object(installer,'data_directory',return_value=root), \
                    patch.object(app,'trusted_keys',return_value=keys),patch.object(installer.subprocess,'Popen') as launch:
                with self.assertRaises(AppUpdateError): installer.apply_update(path,'0'*64)
            self.assertFalse((path.parent/'helper-ready.json').exists())
            self.assertEqual(current.read_bytes(),b'old-app')
            launch.assert_not_called()
