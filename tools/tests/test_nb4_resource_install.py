# SPDX-License-Identifier: GPL-2.0-only
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import nb4_resource_install as resources
from nb4_storage_backup import validate_storage_session


class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(); self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name); self.package=self.root/'package'; self.volume=self.root/'volume'; self.volume.mkdir()
        self.device=dict(serial='000000000001',target='nb4',nonce='ab'*16)
        self.marker=dict(format='apextx-storage-session-1',**self.device)
        (self.volume/'APEXTX-USB.json').write_text(json.dumps(self.marker))
        self.files={}
        for name,data in [('THEMES/test/theme.yml',b'theme'),('SCRIPTS/TOOLS/test.lua',b'tool'),('APEXTX-VERSION.json',b'{"resourceVersion":5}')]:
            source=self.package/'resources'/name; source.parent.mkdir(parents=True,exist_ok=True); source.write_bytes(data)
            self.files[name]=hashlib.sha256(data).hexdigest()

    def install(self): return resources.install(self.package,self.files,self.volume,self.root/'backup',self.device,confirmed=True)

    def test_custom_resources_are_preserved_and_verified_backup_is_kept(self):
        target=self.volume/'THEMES/test/theme.yml'; target.parent.mkdir(parents=True); target.write_bytes(b'personal')
        result=self.install()
        self.assertEqual(result['preserved'],['THEMES/test/theme.yml']); self.assertEqual(target.read_bytes(),b'personal')
        self.assertEqual((self.volume/'SCRIPTS/TOOLS/test.lua').read_bytes(),b'tool')
        self.assertTrue((self.root/'backup/storage.json').is_file())

    def test_volume_swap_is_detected_before_replacement_and_can_resume(self):
        copy=resources.shutil.copyfileobj
        def swap(src,dst,*args,**kwargs):
            copy(src,dst,*args,**kwargs)
            if str(dst.name).endswith('.apxtmp'):
                (self.volume/'APEXTX-USB.json').write_text(json.dumps(dict(self.marker,nonce='cd'*16)))
        with patch.object(resources.shutil,'copyfileobj',side_effect=swap):
            with self.assertRaisesRegex(RuntimeError,'older maintenance session'): self.install()
        self.assertFalse((self.volume/'SCRIPTS/TOOLS/test.lua').exists())
        (self.volume/'APEXTX-USB.json').write_text(json.dumps(self.marker))
        self.assertEqual(self.install()['state'],'complete')

    def test_full_storage_and_tampered_recovery_plan_do_not_write_resources(self):
        from types import SimpleNamespace
        with patch.object(resources.shutil,'disk_usage',return_value=SimpleNamespace(free=0)):
            with self.assertRaisesRegex(RuntimeError,'Not enough storage'): self.install()
        self.assertFalse((self.volume/'SCRIPTS/TOOLS/test.lua').exists())
        pending=self.volume/resources.JOURNAL; record=json.loads(pending.read_text())
        record['writes']['../escape']=dict(before=None,sha256='x',bytes=1); pending.write_text(json.dumps(record))
        with self.assertRaisesRegex(RuntimeError,'private computer record'): self.install()
        self.assertFalse((self.root/'escape').exists())

if __name__=='__main__': unittest.main()
