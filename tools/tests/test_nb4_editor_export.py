# SPDX-License-Identifier: GPL-2.0-only
import base64
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import zipfile
from http.server import ThreadingHTTPServer
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('editor_export_test', ROOT / 'tools/apextx-theme-editor.py')
editor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(editor)


class EditorExports(unittest.TestCase):
    def setUp(self):
        from PIL import Image
        self.temporary = tempfile.TemporaryDirectory()
        self.home = Path(self.temporary.name).resolve() / 'home'
        self.home.mkdir()
        self.home_patch = patch.object(Path, 'home', return_value=self.home)
        self.home_patch.start()
        image = io.BytesIO()
        Image.new('RGB', (320, 480), '#123456').save(image, format='PNG')
        self.png = image.getvalue()
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, 'w') as output:
            output.writestr('THEMES/Demo/theme.yml', 'summary:\n  name: Demo\n')
        self.zip = archive.getvalue()
        self.capture = json.dumps(dict(format='apextx-ui-perf-1', serial='test', firmware='test', samples=[])).encode()

    def tearDown(self):
        self.home_patch.stop()
        self.temporary.cleanup()

    def artifact(self, kind='png', name='preview.png', data=None):
        return dict(kind=kind, name=name, data=base64.b64encode(self.png if data is None else data).decode())

    def test_exports_are_verified_unique_files_in_computer_library(self):
        paths = []
        for kind, name, data in [('png', 'preview.png', self.png), ('png', 'preview.png', self.png),
                                 ('zip', 'Demo.zip', self.zip), ('performance', 'capture.json', self.capture)]:
            result = editor.export_artifact(self.artifact(kind, name, data))
            self.assertTrue(result['ok'])
            path = Path(result['path'])
            self.assertEqual(path.parent, self.home / 'ApexTX-Themes/exports')
            self.assertEqual(path.read_bytes(), data)
            paths.append(path)
        self.assertEqual(len(set(paths)), len(paths))

    def test_file_name_cannot_choose_an_arbitrary_destination(self):
        for name in ('../outside.png', '/tmp/outside.png', r'C:\outside.png', 'folder/out.png', '.hidden.png', 'bad.png\n', 'bad.zip'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                editor.export_artifact(self.artifact(name=name))
        self.assertFalse((self.home / 'ApexTX-Themes').exists())

    def test_invalid_data_is_rejected_before_any_file_is_created(self):
        for artifact in (self.artifact(data=b'not PNG'), self.artifact('zip', 'theme.zip', b'PK\x03\x04bad'),
                         self.artifact('performance', 'capture.json', b'{}'), self.artifact(data=b'x' * (editor.MAX_THEME_BYTES + 1))):
            with self.subTest(kind=artifact['kind']), self.assertRaises(ValueError):
                editor.export_artifact(artifact)
        self.assertFalse((self.home / 'ApexTX-Themes').exists())

    def test_zip_cannot_export_unmanaged_or_traversing_resources(self):
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, 'w') as output:
            output.writestr('../outside', 'contents')
        with self.assertRaisesRegex(ValueError, 'archive contents'):
            editor.export_artifact(self.artifact('zip', 'theme.zip', archive.getvalue()))

    def test_symbolic_export_directory_is_rejected(self):
        library = self.home / 'ApexTX-Themes'
        library.mkdir()
        outside = self.home / 'outside'
        outside.mkdir()
        (library / 'exports').symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Symbolic links'):
            editor.export_artifact(self.artifact())
        self.assertEqual(list(outside.iterdir()), [])

    def test_authenticated_http_exports_preserve_drafts_and_report_write_failures(self):
        token = 'test-editor-export-token'
        server = ThreadingHTTPServer(('127.0.0.1', 0), editor.create_handler(token, None))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        def request(body, authorized=True):
            headers = {'X-ApexTX-Token': token} if authorized else {}
            value = urllib.request.Request(f'http://127.0.0.1:{server.server_port}/api/editor/export',
                                           data=json.dumps(body).encode(), headers=headers)
            return urllib.request.urlopen(value, timeout=3)
        try:
            with self.assertRaises(urllib.error.HTTPError) as denied:
                request({'artifact': self.artifact()}, authorized=False)
            self.assertEqual(denied.exception.code, 403)
            denied.exception.close()
            with request({'artifact': self.artifact()}) as response:
                result = json.load(response)
            self.assertEqual(Path(result['path']).read_bytes(), self.png)
            draft = {'format': 'apextx-editor-draft-1', 'theme': {'name': 'Keep me'}}
            with request({'document': draft}) as response:
                result = json.load(response)
            self.assertEqual(json.loads(Path(result['path']).read_bytes()), draft)
            with patch.object(editor.radio_files, 'write_durable', side_effect=OSError('disk full')):
                with self.assertRaises(urllib.error.HTTPError) as failed:
                    request({'artifact': self.artifact()})
                self.assertEqual(failed.exception.code, 400)
                self.assertEqual(json.load(failed.exception), {'error': 'disk full'})
                failed.exception.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join(3)


if __name__ == '__main__':
    unittest.main()
