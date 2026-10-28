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
                patch('nb4_flasher_session.probe', return_value={'state': 'absent'}), \
                patch.object(usb_update, 'probe', return_value={'state': 'absent'}), \
                patch.object(flasher, 'webbrowser') as browser, \
                patch('builtins.print'):
            flasher.serve()
        self.assertEqual(errors, [])
        browser.open.assert_not_called()


class AutomaticWindow(unittest.TestCase):
    def test_http_auto_update_requires_the_detected_choice_and_keeps_window_open(self):
        from nb4_flasher_session import FlasherSession
        session = FlasherSession(client=Mock())
        session.device = dict(state='ready', serial='A', target=1, model='NB4', version=None)
        session.offer = dict(id=42, version='0.1.0-alpha.4', prerelease=True, target=1, url='https://github.com/')
        session.monitor = Mock()
        entered, finish = threading.Event(), threading.Event()
        errors = []

        class Closing:
            def __iadd__(self, callback):
                self.callback = callback
                return self
        window = types.SimpleNamespace(events=types.SimpleNamespace(closing=Closing()))

        def automatic(device, offer):
            self.assertEqual((device['serial'],offer['id']),('A',42))
            entered.set()
            finish.wait(5)
            session.progress('Verified',100)
            session.finish()
        session.automatic = automatic

        def start():
            try:
                parsed = urllib.parse.urlparse(webview.create_window.call_args.args[1])
                token = urllib.parse.parse_qs(parsed.query)['token'][0]
                base = f'http://{parsed.netloc}'
                def call(path, data=None, authorized=True):
                    headers = {'X-ApexTX-Token':token} if authorized else {}
                    request = urllib.request.Request(base+path,
                        data=json.dumps(data).encode() if data is not None else None, headers=headers)
                    return urllib.request.urlopen(request)
                with call('/api/status') as response:
                    self.assertEqual(json.load(response)['release']['decision'],'unknown')
                for choice in [dict(serial='B',target=1,release_id=42),dict(serial='A',target=2,release_id=42),dict(serial='A',target=1,release_id=99)]:
                    with self.assertRaises(urllib.error.HTTPError) as caught: call('/api/upgrade',choice)
                    self.assertEqual(caught.exception.code,409)
                    caught.exception.close()
                choice=dict(serial='A',target=1,release_id=42)
                with self.assertRaises(urllib.error.HTTPError) as caught: call('/api/upgrade',choice,False)
                self.assertEqual(caught.exception.code,403)
                caught.exception.close()
                with call('/api/upgrade',choice) as response: self.assertEqual(response.status,202)
                self.assertTrue(entered.wait(2))
                self.assertFalse(window.events.closing.callback())
                with self.assertRaises(urllib.error.HTTPError) as caught: call('/api/upgrade',choice)
                self.assertEqual(caught.exception.code,409)
                caught.exception.close()
                with self.assertRaises(urllib.error.HTTPError) as caught: call('/api/channel',dict(preview=False))
                caught.exception.close()
                finish.set()
                deadline=time.monotonic()+2
                while session.snapshot()['running'] and time.monotonic()<deadline: time.sleep(.01)
                self.assertTrue(window.events.closing.callback())
                with call('/api/channel',dict(preview=False)) as response: self.assertEqual(response.status,202)
                self.assertFalse(session.preview)
            except Exception as error:
                errors.append(error)
            finally: finish.set()
        webview=types.SimpleNamespace(create_window=Mock(return_value=window),start=start)
        with patch.dict(sys.modules,{'webview':webview}),patch.dict('os.environ',{'APEXTX_UPDATER_WINDOW':'1'}), \
                patch.object(flasher,'FlasherSession',return_value=session),patch('builtins.print'):
            flasher.serve()
        self.assertEqual(errors,[])


class GuidedBootloaderWindow(unittest.TestCase):
    def test_http_guidance_requires_explicit_install_and_checks_dfu_before_backup(self):
        from nb4_flasher_session import FlasherSession
        from tools.tests.test_nb4_releases import image
        firmware = image(version='0.1.0-alpha.5', bootloader=1)
        for failure in (None, 'swapped', 'backup'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as folder:
                session = FlasherSession(client=Mock())
                session.device = dict(state='ready',serial='A',target=1,model='NB4',version=None,bootloader=0)
                session.offer = dict(id=42,version='0.1.0-alpha.5',target=1,prerelease=True,url='https://github.com/',bootloader=1)
                session.client.download.return_value = firmware
                session.monitor = Mock()
                entered, finish = threading.Event(), threading.Event()
                errors = []
                class Closing:
                    def __iadd__(self, callback):
                        self.callback = callback
                        return self
                window = types.SimpleNamespace(events=types.SimpleNamespace(closing=Closing()))
                def install(data, progress, serial):
                    self.assertEqual((data,serial),(firmware,'A'))
                    entered.set()
                    if not finish.wait(5): raise RuntimeError('Test did not finish')
                    progress('Installation complete and verified. The radio is starting automatically.',100)
                def backup(path, _progress, serial):
                    self.assertEqual(serial,'A')
                    if failure == 'backup': raise RuntimeError('Backup failed')
                    path.write_bytes(firmware)
                def wait_idle():
                    deadline = time.monotonic() + 3
                    while session.snapshot()['running'] and time.monotonic() < deadline: time.sleep(.01)
                    self.assertFalse(session.snapshot()['running'])
                def start():
                    try:
                        parsed = urllib.parse.urlparse(webview.create_window.call_args.args[1])
                        token = urllib.parse.parse_qs(parsed.query)['token'][0]
                        def call(path, data):
                            req = urllib.request.Request(f'http://{parsed.netloc}'+path,
                                data=json.dumps(data).encode(),headers={'X-ApexTX-Token':token})
                            return urllib.request.urlopen(req,timeout=3)
                        def rejected(path):
                            with self.assertRaises(urllib.error.HTTPError) as caught: call(path,choice)
                            self.assertEqual(caught.exception.code,409)
                            caught.exception.close()
                        choice = dict(serial='A',target=1,release_id=42)
                        rejected('/api/upgrade')
                        rejected('/api/bootloader/install')
                        with call('/api/bootloader/prepare',choice) as response: self.assertEqual(response.status,202)
                        wait_idle()
                        self.assertIsNotNone(session.snapshot()['bootloader_install'])
                        writer.assert_not_called()
                        saver.assert_not_called()
                        rejected('/api/bootloader/install')
                        session.device = dict(state='recovery',serial='B')
                        rejected('/api/bootloader/install')
                        session.device = dict(state='recovery',serial='A')
                        writer.assert_not_called()
                        with call('/api/bootloader/install',choice) as response: self.assertEqual(response.status,202)
                        if failure is None:
                            self.assertTrue(entered.wait(2))
                            self.assertFalse(window.events.closing.callback())
                            rejected('/api/bootloader/install')
                            finish.set()
                        wait_idle()
                        if failure:
                            self.assertTrue(session.snapshot()['error'])
                            writer.assert_not_called()
                            self.assertIsNotNone(session.snapshot()['bootloader_install'])
                            if failure == 'swapped': saver.assert_not_called()
                        else:
                            self.assertEqual(session.snapshot()['progress'],100)
                            self.assertIsNone(session.snapshot()['bootloader_install'])
                        self.assertEqual(window.events.closing.callback(), failure is None)
                        if failure:
                            # A pending guide and uncertain outcome both block
                            # desktop replacement and normal window closure.
                            with self.assertRaises(RuntimeError): session.reserve_app_update()
                            with call('/api/bootloader/cancel', {}) as response: self.assertEqual(response.status, 202)
                            self.assertFalse(window.events.closing.callback())
                            with call('/api/app-update/acknowledge-radio', {}) as response: self.assertEqual(response.status, 202)
                            self.assertTrue(window.events.closing.callback())
                    except Exception as error:
                        errors.append(error)
                    finally: finish.set()
                webview=types.SimpleNamespace(create_window=Mock(return_value=window),start=start)
                with patch.dict(sys.modules,{'webview':webview}),patch.dict('os.environ',{'APEXTX_UPDATER_WINDOW':'1'}), \
                        patch.object(flasher,'FlasherSession',return_value=session),patch.object(pathlib.Path,'home',return_value=pathlib.Path(folder)), \
                        patch.object(flasher.nb4_rom_flash,'discover_rom_dfu',return_value='B' if failure=='swapped' else 'A'), \
                        patch.object(flasher.nb4_rom_flash,'backup_internal',side_effect=backup) as saver, \
                        patch.object(flasher.nb4_rom_flash,'run_install',side_effect=install) as writer,patch('builtins.print'):
                    flasher.serve()
                self.assertEqual(errors,[])


class UpdatedAppStartup(unittest.TestCase):
    def test_failed_health_handshake_keeps_usb_blocked_but_allows_safe_window_close(self):
        session=flasher.FlasherSession(client=Mock())
        session.monitor=Mock()
        errors=[]
        class Closing:
            def __iadd__(self, callback):
                self.callback=callback
                return self
        window=types.SimpleNamespace(events=types.SimpleNamespace(closing=Closing()))
        def start():
            try:
                url=webview.create_window.call_args.args[1]
                parsed=urllib.parse.urlparse(url)
                token=urllib.parse.parse_qs(parsed.query)['token'][0]
                request=urllib.request.Request(f'http://{parsed.netloc}/api/status',headers={'X-ApexTX-Token':token})
                with urllib.request.urlopen(request,timeout=3) as response:
                    state=json.load(response)
                self.assertTrue(state['app_updating'])
                self.assertEqual(state['app_update']['state'],'error')
                session.monitor.assert_not_called()
                with self.assertRaises(RuntimeError): session.begin('upgrade')
                self.assertTrue(window.events.closing.callback(),'No radio was started; let the user close and recover')
            except Exception as error:
                errors.append(error)
        webview=types.SimpleNamespace(create_window=Mock(return_value=window),start=start)
        with patch.dict(sys.modules,{'webview':webview}),patch.dict('os.environ',{'APEXTX_UPDATER_WINDOW':'1'}), \
                patch.object(flasher,'FlasherSession',return_value=session),patch('builtins.print'):
            flasher.serve(startup_health=Mock(side_effect=RuntimeError('Helper confirmation failed')))
        self.assertEqual(errors,[])


if __name__ == '__main__':
    unittest.main()
