# SPDX-License-Identifier: GPL-2.0-only
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.parse
import urllib.request
from http.server import ThreadingHTTPServer
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('editor_performance_test', ROOT / 'tools/apextx-theme-editor.py')
editor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(editor)


class PerformanceAPI(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        (self.root / 'RADIO').mkdir()
        (self.root / 'RADIO/radio.yml').write_text('radio settings unchanged')
        self.capture = self.root / 'APEXTX-UI-PERF.json'
        self.valid = dict(format='apextx-ui-perf-1', serial='NB4-test', firmware='revision',
                          samples=[dict(fps=60, pixels=1024, areas=2, loopAvgUs=200, lvglAvgUs=100)])
        self.targets = patch.object(editor, 'theme_targets', return_value=[dict(path=str(self.root), settings=True)])
        self.targets.start()
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), editor.create_handler('performance-token', None))
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(3)
        self.targets.stop()
        self.temporary.cleanup()

    def request(self, target=None, authorized=True):
        query = urllib.parse.urlencode({'target': str(self.root) if target is None else target})
        request = urllib.request.Request(f'http://127.0.0.1:{self.server.server_port}/api/editor/perf?{query}',
                                         headers={'X-ApexTX-Token': 'performance-token'} if authorized else {})
        try:
            response = urllib.request.urlopen(request, timeout=3)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return response.status, json.load(response)

    def test_authenticated_capture_read_is_read_only(self):
        original = json.dumps(self.valid).encode()
        self.capture.write_bytes(original)
        self.assertEqual(self.request(authorized=False)[0], 403)
        self.assertEqual(self.request(), (200, self.valid))
        self.assertEqual(self.capture.read_bytes(), original)
        self.assertEqual((self.root / 'RADIO/radio.yml').read_text(), 'radio settings unchanged')
        self.assertEqual(sorted(path.name for path in self.root.iterdir()), ['APEXTX-UI-PERF.json', 'RADIO'])

    def test_offline_missing_and_disconnected_states_are_distinct(self):
        self.assertEqual(self.request('')[1]['code'], 'perf_no_radio')
        code, response = self.request()
        self.assertEqual(code, 400)
        self.assertEqual(response['code'], 'perf_missing')
        self.assertNotIn(str(self.root), response['error'], 'Missing capture should not expose a raw filesystem exception')
        (self.root / 'RADIO/radio.yml').unlink()
        self.assertEqual(self.request()[1]['code'], 'perf_disconnected')

    def test_invalid_legacy_empty_and_oversized_captures_never_look_successful(self):
        for data, expected in [
            (b'{truncated', 'perf_invalid'),
            (b'{"format":"legacy","samples":[]}', 'perf_unsupported'),
            (json.dumps({**self.valid, 'samples': []}).encode(), 'perf_empty'),
            (json.dumps({**self.valid, 'samples': [{}]}).encode(), 'perf_invalid'),
            (json.dumps({**self.valid, 'samples': [self.valid['samples'][0]] * 31}).encode(), 'perf_invalid'),
            (b' ' * 65537, 'perf_invalid'),
        ]:
            with self.subTest(expected=expected):
                self.capture.write_bytes(data)
                status, response = self.request()
                self.assertEqual(status, 400)
                self.assertEqual(response['code'], expected)

    def test_symbolic_capture_is_not_followed(self):
        outside = self.root / 'outside.json'
        outside.write_text(json.dumps(self.valid))
        self.capture.symlink_to(outside)
        self.assertEqual(self.request()[1]['code'], 'perf_invalid')


if __name__ == '__main__':
    unittest.main()
