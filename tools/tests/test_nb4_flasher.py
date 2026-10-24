# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the shared GUI lifecycle and frozen USB runtime, without a radio."""
import importlib.util
import json
import pathlib
import sys
import tempfile
import threading
import time
import types
import unittest
import urllib.parse
import urllib.request
from unittest.mock import Mock, patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import nb4_usb_update as usb_update
from tools.tests.test_nb4_updater import fixture

spec = importlib.util.spec_from_file_location('apextx_flasher', ROOT / 'tools/apextx-updater.py')
flasher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flasher)


class BundledUsb(unittest.TestCase):
    def modules(self):
        usb = types.ModuleType('usb')
        usb.core = types.ModuleType('usb.core')
        usb.util = types.ModuleType('usb.util')
        usb.backend = types.ModuleType('usb.backend')
        usb.backend.libusb1 = types.ModuleType('usb.backend.libusb1')
        usb.backend.libusb1.get_backend = Mock(return_value=object())
        return {name: module for name, module in (
            ('usb', usb), ('usb.core', usb.core), ('usb.util', usb.util),
            ('usb.backend', usb.backend), ('usb.backend.libusb1', usb.backend.libusb1))}

    def test_frozen_apps_load_their_own_runtime_on_both_platforms(self):
        for platform, filename in [('darwin', 'libusb-1.0.0.dylib'),
                                   ('win32', 'libusb-1.0.dll')]:
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as folder:
                library = pathlib.Path(folder) / filename
                library.touch()
                modules = self.modules()
                loader = modules['usb.backend.libusb1'].get_backend
                with patch.dict(sys.modules, modules), \
                        patch.object(sys, 'frozen', True, create=True), \
                        patch.object(sys, '_MEIPASS', folder, create=True), \
                        patch.object(sys, 'platform', platform):
                    usb_update.usb_modules()
                loader.assert_called_once()
                finder = loader.call_args.kwargs['find_library']
                self.assertEqual(finder('usb-1.0'), str(library))

    def test_incomplete_bundle_never_silently_uses_a_host_library(self):
        modules = self.modules()
        with tempfile.TemporaryDirectory() as folder, patch.dict(sys.modules, modules), \
                patch.object(sys, 'frozen', True, create=True), \
                patch.object(sys, '_MEIPASS', folder, create=True), \
                patch.object(sys, 'platform', 'darwin'):
            with self.assertRaisesRegex(usb_update.UpdateError, 'missing its bundled libusb'):
                usb_update.usb_modules()
        modules['usb.backend.libusb1'].get_backend.assert_not_called()


class SharedWindow(unittest.TestCase):
    def test_window_stays_open_during_transfer_and_can_close_afterwards(self):
        entered, finish = threading.Event(), threading.Event()
        errors = []

        class Closing:
            def __iadd__(self, callback):
                self.callback = callback
                return self

        window = types.SimpleNamespace(events=types.SimpleNamespace(closing=Closing()))

        def transfer(_image, _progress):
            entered.set()
            if not finish.wait(5):
                raise RuntimeError('Test did not release the transfer')

        def start():
            try:
                url = webview.create_window.call_args.args[1]
                parsed = urllib.parse.urlparse(url)
                token = urllib.parse.parse_qs(parsed.query)['token'][0]
                base = f'http://{parsed.netloc}'
                headers = {'X-ApexTX-Token': token}
                self.assertTrue(window.events.closing.callback())
                with urllib.request.urlopen(url) as response:
                    page = response.read().decode()
                self.assertIn('ApexTX', page)
                self.assertNotIn('__SESSION_TOKEN__', page)
                self.assertNotIn('__HOST_PLATFORM__', page)
                self.assertIn(f'data-platform="{sys.platform}"', page)
                request = urllib.request.Request(base + '/api/update', data=fixture(), headers=headers)
                with urllib.request.urlopen(request) as response:
                    self.assertEqual(response.status, 202)
                self.assertTrue(entered.wait(2))
                self.assertFalse(window.events.closing.callback())
                finish.set()
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    request = urllib.request.Request(base + '/api/status', headers=headers)
                    with urllib.request.urlopen(request) as response:
                        if not json.load(response)['running']:
                            break
                    time.sleep(0.01)
                self.assertTrue(window.events.closing.callback())
            except Exception as error:
                errors.append(error)
            finally:
                finish.set()

        webview = types.SimpleNamespace(create_window=Mock(return_value=window), start=start)
        with patch.dict(sys.modules, {'webview': webview}), \
                patch.dict('os.environ', {'APEXTX_UPDATER_WINDOW': '1'}), \
                patch.object(flasher, 'run_update', side_effect=transfer), \
                patch.object(flasher, 'webbrowser') as browser, \
                patch('builtins.print'):
            flasher.serve()
        self.assertEqual(errors, [])
        browser.open.assert_not_called()


if __name__ == '__main__':
    unittest.main()
