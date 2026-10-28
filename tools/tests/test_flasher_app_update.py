# SPDX-License-Identifier: GPL-2.0-only
import base64
import hashlib
import io
import json
import pathlib
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
import flasher_app_update as app
from nb4_flasher_session import FlasherSession


def signed_release(version='1.5.0', build=15000):
    key = Ed25519PrivateKey.generate()
    content = b'authenticated executable fixture'
    name = f'ApexTX-Flasher-{version}-macos-arm64.zip'
    doc = dict(schema=1, product='org.apextx.flasher', version=version, build=build,
        channel='preview' if '-' in version else 'stable', revision='a' * 40,
        updater_protocol=1, notes='App notes <script>not HTML</script>',
        packages={'macos-arm64': dict(name=name, size=len(content), minimum_os='15.0',
                                     sha256=hashlib.sha256(content).hexdigest())})
    raw = json.dumps(doc).encode()
    keys = {'test': base64.b64encode(key.public_key().public_bytes_raw()).decode()}
    return doc, raw, key.sign(raw), keys, content


def wait_for(predicate):
    deadline = time.monotonic() + 3
    while not predicate():
        if time.monotonic() > deadline:
            raise AssertionError('Worker did not finish')
        time.sleep(.01)


class Authentication(unittest.TestCase):
    def test_signature_authenticates_exact_bytes_and_pins_identity(self):
        doc, raw, signature, keys, _ = signed_release()
        self.assertEqual(app.verify_manifest(raw, signature, keys), doc)
        for content, sig, trust in ((raw+b' ', signature, keys), (raw, signature, {}),
                                    (raw, signature[:-1], keys), (raw, b'0'*64, keys)):
            with self.subTest(content=content[:15], signature=len(sig), trust=bool(trust)):
                with self.assertRaises(app.AppUpdateError): app.verify_manifest(content, sig, trust)

    def test_valid_signature_cannot_authorize_wrong_product_layout_or_schema(self):
        doc, _, _, keys, _ = signed_release()
        key = Ed25519PrivateKey.generate()
        keys = {'new': base64.b64encode(key.public_key().public_bytes_raw()).decode()}
        mutations = [dict(product='firmware'), dict(schema=2), dict(updater_protocol=2),
                     dict(channel='preview'), dict(version='1.5'), dict(build=0), dict(build=True)]
        for change in mutations:
            raw = json.dumps(dict(doc, **change)).encode()
            with self.subTest(change=change):
                with self.assertRaises(app.AppUpdateError): app.verify_manifest(raw, key.sign(raw), keys)
        item = doc['packages']['macos-arm64']
        for change in (dict(name='../bad.zip'), dict(size=app.MAX_PACKAGE+1), dict(sha256='x'*64), dict(minimum_os='bad')):
            raw = json.dumps(dict(doc, packages={'macos-arm64': dict(item, **change)})).encode()
            with self.assertRaises(app.AppUpdateError): app.verify_manifest(raw, key.sign(raw), keys)
        raw = b'{"schema":1,"schema":1}'
        with self.assertRaises(app.AppUpdateError): app.verify_manifest(raw, key.sign(raw), keys)


class ReleaseDiscovery(unittest.TestCase):
    def setUp(self):
        self.doc, self.raw, self.signature, self.keys, self.content = signed_release()
        self.tag = 'flasher-v' + self.doc['version']
        self.base = f'https://github.com/{app.REPOSITORY}/releases/download/{self.tag}/'
        self.files = {app.MANIFEST:self.raw, app.SIGNATURE:self.signature,
                      self.doc['packages']['macos-arm64']['name']:self.content}
        self.row = dict(tag_name=self.tag, id=42, draft=False, prerelease=False,
            assets=[dict(name=name, browser_download_url=self.base+name, size=len(data), state='uploaded')
                    for name, data in self.files.items()])
        self.client = app.AppReleaseClient(opener=Mock())
        self.client.fetch = Mock(side_effect=self.fetch)

    def fetch(self, url, limit, *args):
        if url.startswith(app.API):
            return json.dumps([dict(tag_name='v99.0.0', draft=False), self.row]).encode()
        return self.files[url.removeprefix(self.base)]

    def latest(self, installed='1.4.0', preview=False, highest=0):
        with patch.object(app, 'trusted_keys', return_value=self.keys), patch.object(app, 'os_compatible', return_value=True):
            return self.client.latest('macos-arm64', preview, installed, highest)

    def test_selects_app_without_radio_or_firmware_tag_confusion(self):
        offer = self.latest()
        self.assertEqual(offer['document']['version'], '1.5.0')
        self.assertTrue(offer['compatible'])
        self.assertIsNone(self.latest(installed='1.5.0'))
        self.assertIsNone(self.latest(installed='2.0.0'))

    def test_preview_is_explicit_and_drafts_are_never_used(self):
        self.row['prerelease'] = True
        self.assertIsNone(self.latest())
        self.assertIsNotNone(self.latest(preview=True))
        self.row['draft'] = True
        self.assertIsNone(self.latest(preview=True))

    def test_duplicate_missing_foreign_assets_and_rollback_fail_closed(self):
        with self.assertRaises(app.AppUpdateError): self.latest(highest=99999)
        self.row['assets'].append(self.row['assets'][0])
        with self.assertRaises(app.AppUpdateError): self.latest()
        self.row['assets'].pop()
        self.row['assets'][0]['browser_download_url'] = 'https://evil.example/update'
        with self.assertRaises(app.AppUpdateError): self.latest()
        self.row['assets'] = []
        with self.assertRaises(app.AppUpdateError): self.latest()

    def test_wrong_platform_is_not_downloaded_and_os_requirement_is_reported(self):
        with patch.object(app, 'trusted_keys', return_value=self.keys), patch.object(app, 'os_compatible', return_value=False):
            offer = self.client.latest('macos-arm64', False, '1.4.0')
            self.assertFalse(offer['compatible'])

    def test_bounded_pagination_does_not_report_current_when_incomplete(self):
        self.client.fetch = Mock(return_value=json.dumps([dict(tag_name='v1.0.0')]*100).encode())
        with self.assertRaisesRegex(app.AppUpdateError, 'incomplete'):
            self.latest()
        self.assertEqual(self.client.fetch.call_count, 10)


class Transport(unittest.TestCase):
    def response(self, content, length=None):
        result = io.BytesIO(content)
        result.headers = {'Content-Length': str(len(content) if length is None else length)}
        return result

    def test_streamed_download_hash_size_and_cleanup(self):
        doc, raw, signature, _, content = signed_release()
        item = doc['packages']['macos-arm64']
        offer = dict(package=item, url='https://github.com/test', raw=raw, signature=signature)
        for incoming in (content, content[:-1], b'x'*len(content)):
            with tempfile.TemporaryDirectory() as directory:
                client = app.AppReleaseClient(opener=Mock(open=Mock(return_value=self.response(incoming))))
                folder = pathlib.Path(directory)
                if incoming == content:
                    self.assertEqual(client.download(offer, folder, Mock()).read_bytes(), content)
                else:
                    with self.assertRaises(app.AppUpdateError): client.download(offer, folder, Mock())
                self.assertFalse((folder/'download.part').exists())

    def test_host_size_and_truncation_limits(self):
        client = app.AppReleaseClient(opener=Mock())
        for url in ('http://github.com/x', 'https://evil.example/x', 'https://github.com:444/x'):
            with self.assertRaises(app.AppUpdateError): client.fetch(url, 10)
        for content, length in ((b'ab', 20), (b'ab', 3)):
            client.opener.open.return_value = self.response(content, length)
            with self.assertRaises(app.AppUpdateError): client.fetch('https://github.com/x', 10)

    def test_redirect_allowlist_is_retained(self):
        with self.assertRaises(RuntimeError):
            app._Redirects().redirect_request(Mock(), None, 302, '', {}, 'https://evil.example/x')

    def test_etag_cache_and_rate_limit_backoff(self):
        import urllib.error
        from email.message import Message
        client = app.AppReleaseClient(opener=Mock())
        response=self.response(b'[]')
        response.headers['ETag']='"catalog-1"'
        client.opener.open.return_value=response
        url=app.API+'/releases?per_page=100&page=1'
        self.assertEqual(client.fetch(url,100),b'[]')
        client.opener.open.side_effect=urllib.error.HTTPError(url,304,'Not modified',{},None)
        self.assertEqual(client.fetch(url,100),b'[]')
        self.assertEqual(client.opener.open.call_args.args[0].get_header('If-none-match'),'"catalog-1"')
        headers=Message()
        headers['Retry-After']='7200'
        client.opener.open.side_effect=urllib.error.HTTPError(url,429,'Limited',headers,None)
        with self.assertRaises(app.AppUpdateError) as caught: client.fetch(url,100)
        self.assertEqual(caught.exception.retry_seconds,7200)


class Lifecycle(unittest.TestCase):
    def test_radio_and_app_reservations_are_mutually_exclusive(self):
        for operation in ('upgrade','manual','recover','bootloader','audio','maintenance'):
            session = FlasherSession(client=Mock())
            session.begin(operation)
            with self.assertRaises(RuntimeError): session.reserve_app_update()
            session.finish()
            session.reserve_app_update()
            with self.assertRaises(RuntimeError): session.begin(operation)

    def test_pending_guide_and_failed_radio_write_remain_blocked(self):
        session = FlasherSession(client=Mock())
        session.bootloader_install = {'image': b'pending'}
        with self.assertRaises(RuntimeError): session.reserve_app_update()
        session.bootloader_install = None
        session.begin('upgrade')
        session.finish(RuntimeError('verification outcome unknown'))
        self.assertTrue(session.app_restart_blocked())
        with self.assertRaises(RuntimeError): session.reserve_app_update()
        session.acknowledge_radio()
        session.reserve_app_update()

    def test_racing_requests_cannot_both_win(self):
        for _ in range(30):
            session = FlasherSession(client=Mock())
            barrier = threading.Barrier(2)
            won = []
            def compete(kind):
                barrier.wait()
                try:
                    (session.reserve_app_update if kind == 'app' else lambda: session.begin('upgrade'))()
                    won.append(kind)
                except RuntimeError:
                    pass
            threads = [threading.Thread(target=compete,args=(kind,)) for kind in ('app','radio')]
            for thread in threads: thread.start()
            for thread in threads: thread.join()
            self.assertEqual(len(won),1)

    def test_process_lease_blocks_another_copy_and_can_be_released(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            lease = app.InstanceLease(folder)
            with self.assertRaises(app.AppUpdateError): app.InstanceLease(folder)
            lease.close()
            app.InstanceLease(folder).close()

    def test_defer_channel_and_offline_state_persist_without_restarting(self):
        with tempfile.TemporaryDirectory() as directory:
            session, restart, client = FlasherSession(client=Mock()), Mock(), Mock()
            folder = pathlib.Path(directory)
            controller = app.AppUpdateController(session,restart,folder,client)
            doc, raw, signature, keys, _ = signed_release()
            client.latest.return_value = dict(document=doc, raw=raw, signature=signature, compatible=True, package=doc['packages']['macos-arm64'])
            controller.check(preview=False)
            wait_for(lambda: not controller.checking)
            controller.defer(skip=True)
            self.assertTrue(controller.snapshot()['hidden'])
            controller.check(manual=True)
            wait_for(lambda: not controller.checking)
            self.assertFalse(controller.snapshot()['hidden'])
            controller.defer()
            self.assertTrue(controller.snapshot()['hidden'])
            copy = app.AppUpdateController(session,restart,folder,client)
            self.assertFalse(copy.preview)
            client.latest.side_effect = app.AppUpdateError('Offline')
            controller.check()
            wait_for(lambda: not controller.checking)
            self.assertEqual(controller.snapshot()['state'], 'error')
            self.assertFalse(session.status['running'])
            restart.assert_not_called()
            client.download.assert_not_called()

    def test_failed_install_releases_reservation_and_never_restarts(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(sys, 'frozen', True, create=True), patch.object(app, 'app_version', return_value='1.4.0'), patch.object(app, 'app_build', return_value=14000):
            session, restart, client = FlasherSession(client=Mock()), Mock(), Mock()
            controller = app.AppUpdateController(session,restart,pathlib.Path(directory),client)
            doc, raw, signature, _, _ = signed_release()
            controller.offer = dict(document=doc, raw=raw, signature=signature, compatible=True, package=doc['packages']['macos-arm64'])
            client.download.side_effect = OSError('No network')
            controller.install(doc['version'])
            wait_for(lambda: not session.app_updating)
            self.assertEqual(controller.snapshot()['state'],'error')
            restart.assert_not_called()


class CachedOffers(unittest.TestCase):
    def test_signed_offer_survives_restart_and_channel_failure_clears_preview(self):
        with tempfile.TemporaryDirectory() as directory:
            folder=pathlib.Path(directory)
            doc,raw,signature,keys,_=signed_release('1.5.0-alpha.1',15001)
            session=FlasherSession(client=Mock())
            client=Mock()
            client.latest.return_value=dict(document=doc,raw=raw,signature=signature,
                package=doc['packages']['macos-arm64'],compatible=True)
            with patch.object(app,'trusted_keys',return_value=keys),patch.object(app,'target_platform',return_value='macos-arm64'),patch.object(app,'os_compatible',return_value=True):
                controller=app.AppUpdateController(session,Mock(),folder,client)
                controller.check(preview=True)
                wait_for(lambda:not controller.checking)
                cached=app.AppUpdateController(session,Mock(),folder,client)
                self.assertEqual(cached.snapshot()['available'],'1.5.0-alpha.1')
                client.latest.side_effect=app.AppUpdateError('Offline')
                cached.check(preview=False)
                wait_for(lambda:not cached.checking)
                self.assertIsNone(cached.offer,'Changing to stable must never leave an installable alpha after network failure')
