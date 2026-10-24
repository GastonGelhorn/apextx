# SPDX-License-Identifier: GPL-2.0-only
"""Release/model selection, bounded downloads, and all pre-write gates."""
import hashlib
import io
import json
import pathlib
import struct
import sys
import unittest
import zipfile
from unittest.mock import MagicMock, Mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from nb4_releases import (API, ReleaseClient, ReleaseError, Version,
                          firmware_identity, update_decision, _Redirects)
from nb4_update_image import finalize
from tools.tests.test_nb4_updater import application


def image(target=1, version='0.1.0-alpha.4', bootloader=0):
    app = application(b'nb4' if target == 1 else b'nb4plus')
    stamp = f'apextx-nb4-2.12.5-apextx-{version} (123456789)\0'.encode()
    app[128:128 + len(stamp)] = stamp
    boot = bytearray(b'\x00' * 0x20000)
    struct.pack_into('<II', boot, 0, 0x20020000, 0x08000101)
    if bootloader:
        prefix = f'APXINFO{bootloader};'.encode()
        boot[256:256+len(prefix)] = prefix
    return finalize(boot + app)


def asset(name, version='0.1.0-alpha.4', size=2097152):
    return dict(name=name, state='uploaded', size=size,
                browser_download_url=f'https://github.com/GastonGelhorn/apextx/releases/download/v{version}/{name}')


def row(version='0.1.0-alpha.4', target='nb4', direct=True):
    value = dict(id=42, tag_name='v' + version, draft=False, prerelease='-' in version,
        assets=[asset(f'apextx-{target}-{version}.bin' if direct else f'apextx-{target}-{version}.zip', version),
                asset('SHA256SUMS', version, 100)])
    if Version(version) > Version('0.1.0-alpha.5'):
        value['assets'].append(asset(f'apextx-{target}-{version}.update.json', version, 300))
    return value


class Versions(unittest.TestCase):
    def test_semantic_ordering_preview_numbers_and_stable(self):
        expected = ['0.1.0-alpha', '0.1.0-alpha.2', '0.1.0-alpha.10', '0.1.0-beta', '0.1.0-rc.1', '0.1.0', '0.2.0', '0.10.0', '1.0.0']
        self.assertEqual([v.text for v in sorted(map(Version, reversed(expected)))], expected)
        for bad in ['v1.0', '01.0.0', '1.0.0-alpha..1', '1.0.0-alpha.01', '1.0.0-', '1.0.0/a']:
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                Version(bad)

    def test_only_application_apextx_stamp_reports_installed_release(self):
        self.assertEqual(firmware_identity('apextx-nb4-2.12.5-apextx-0.1.0-alpha.4-dev (38f7d3507)'),
            dict(version='0.1.0-alpha.4', revision='38f7d3507', development=True))
        for unknown in ['2.12', '2.00', 'DFU Config', 'edgetx-nb4-2.12.5 (38f7d3507)', 'apextx-nb4p-2.12.5-apextx-0.1.0 (38f7d3507)']:
            self.assertIsNone(firmware_identity(unknown)['version'])

    def test_unknown_current_newer_and_development_are_distinct(self):
        offer = dict(version='0.1.0-alpha.4')
        for version, development, expected in [(None, False, 'unknown'), ('0.1.0-alpha.3', False, 'available'),
            ('0.1.0-alpha.4', False, 'current'), ('0.1.0-alpha.5', False, 'ahead'),
            ('0.1.0-alpha.3', True, 'available'), ('0.1.0-alpha.4', True, 'development'),
            ('0.1.0-alpha.5', True, 'development')]:
            self.assertEqual(update_decision(dict(version=version, development=development), offer), expected)

    def test_bootloader_generation_is_independent_of_the_application_version(self):
        offer = dict(version='0.1.0-alpha.5', bootloader=1)
        for version in (None, '0.1.0-alpha.4', '0.1.0-alpha.5'):
            self.assertEqual(update_decision(dict(version=version, bootloader=0), offer), 'bootloader')
        self.assertEqual(update_decision(dict(version='0.1.0-alpha.4', bootloader=1), offer), 'available')
        self.assertEqual(update_decision(dict(version='0.1.0-alpha.5', bootloader=1), offer), 'current')
        self.assertEqual(update_decision(dict(version='0.2.0', bootloader=0), offer), 'ahead')


class Releases(unittest.TestCase):
    def client(self, rows):
        client = ReleaseClient(opener=Mock())
        def fetch(url, *_args):
            if '.update.json' in url:
                from runpy import run_path
                make_metadata = run_path(str(ROOT/'tools/nb4-release-metadata.py'))['metadata']
                for value in rows:
                    if any(a['browser_download_url'] == url for a in value['assets']):
                        target = 2 if 'nb4plus' in url else 1
                        version = value['tag_name'][1:]
                        return json.dumps(make_metadata(image(target, version, 1), version, 'nb4plus' if target == 2 else 'nb4')).encode()
                raise AssertionError(url)
            return json.dumps(rows).encode()
        client.fetch = Mock(side_effect=fetch)
        return client

    def test_legacy_alpha5_requires_the_new_bootloader_and_future_metadata_is_mandatory(self):
        self.assertEqual(self.client([row('0.1.0-alpha.5')]).latest(1)['bootloader'], 1)
        future = row('0.1.0-alpha.6')
        future['assets'].pop()
        with self.assertRaisesRegex(ReleaseError, 'missing bootloader requirements'):
            self.client([future]).latest(1)

    def test_release_requirements_are_validated_before_offering_a_download(self):
        release = row('0.1.0-alpha.6')
        base = dict(schema=1,version='0.1.0-alpha.6',target=1,bootloader=2,
                    firmware='apextx-nb4-0.1.0-alpha.6.bin',sha256='a'*64)
        for changes in (dict(bootloader=True),dict(bootloader=0),dict(target=2),dict(version='0.1.0-alpha.5'),dict(sha256='bad')):
            client = self.client([release])
            client.fetch.side_effect = [json.dumps([release]).encode(), json.dumps(dict(base,**changes)).encode()]
            with self.assertRaisesRegex(ReleaseError, 'invalid bootloader requirements'):
                client.latest(1)

    def test_download_refuses_bootloader_metadata_that_disagrees_with_the_image(self):
        client, release = self.setup_download()
        release['bootloader'] = 2
        with self.assertRaisesRegex(ReleaseError, 'bootloader requirements'):
            client.download(release, Mock())

    def test_metadata_is_derived_from_each_actual_image_not_the_release_name(self):
        from runpy import run_path
        make_metadata = run_path(str(ROOT/'tools/nb4-release-metadata.py'))['metadata']
        for target, model in ((1,'nb4'),(2,'nb4plus')):
            data = image(target, '0.1.0-alpha.6', 2)
            policy = make_metadata(data, '0.1.0-alpha.6', model)
            self.assertEqual((policy['target'],policy['bootloader']), (target,2))
            self.assertEqual(policy['sha256'], hashlib.sha256(data).hexdigest())
            with self.assertRaises(ValueError): make_metadata(data, '0.1.0-alpha.5', model)
            with self.assertRaises(ValueError): make_metadata(data, '0.1.0-alpha.6', 'nb4plus' if target == 1 else 'nb4')

    def test_future_bootloader_release_metadata_and_download_agree_for_both_radios(self):
        from runpy import run_path
        make_metadata = run_path(str(ROOT/'tools/nb4-release-metadata.py'))['metadata']
        for target, model in ((1,'nb4'),(2,'nb4plus')):
            data = image(target, '0.1.0-alpha.6', 2)
            policy = json.dumps(make_metadata(data, '0.1.0-alpha.6', model)).encode()
            release_row = row('0.1.0-alpha.6',model)
            release_row['assets'][-1]['digest']='sha256:'+hashlib.sha256(policy).hexdigest()
            client = self.client([release_row])
            client.fetch.side_effect=[json.dumps([release_row]).encode(),policy]
            release = client.latest(target)
            self.assertEqual(release['bootloader'],2)
            sums=(hashlib.sha256(data).hexdigest()+'  '+release['asset']['name']+'\n').encode()
            client.fetch.side_effect=[sums,data]
            self.assertEqual(client.download(release,Mock()),data)
            client.fetch.side_effect=[sums,data]
            release['image_sha256']='0'*64
            with self.assertRaisesRegex(ReleaseError,'bootloader requirements'): client.download(release,Mock())

    def test_latest_is_semantic_compatible_and_honors_channel(self):
        rows = [row('0.1.0-alpha.9'), row('0.1.0-alpha.10'), row('0.1.0'), row('1.0.0', 'nb4plus')]
        rows[0]['draft'] = True
        client = self.client(rows)
        self.assertEqual(client.latest(1)['version'], '0.1.0')
        self.assertEqual(client.latest(2)['version'], '1.0.0')
        self.assertIsNone(self.client([row()]).latest(1, preview=False))
        self.assertEqual(self.client([row()]).latest(1, preview=True)['version'], '0.1.0-alpha.4')
        self.assertEqual(self.client([row('0.1.0-alpha.9'), row('0.1.0-alpha.10')]).latest(1)['version'], '0.1.0-alpha.10')

    def test_missing_checksum_wrong_url_and_duplicate_assets(self):
        release = row()
        release['assets'].pop()
        self.assertIsNone(self.client([release]).latest(1))
        release = row()
        release['assets'][0]['browser_download_url'] = 'https://example.com/a.bin'
        with self.assertRaises(ReleaseError): self.client([release]).latest(1)
        release = row()
        release['assets'].append(dict(release['assets'][0]))
        self.assertIsNone(self.client([release]).latest(1))

    def setup_download(self, target=1, archive=False, content=None):
        version = '0.1.0-alpha.4'
        data = content if content is not None else image(target)
        if archive:
            root = f'apextx-nb4-{version}/'
            manifest = dict(release=version, target='NB4', files={'firmware.bin':dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())})
            buffer = io.BytesIO()
            with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_DEFLATED) as z:
                z.writestr(root + 'firmware.bin', data)
                z.writestr(root + 'manifest.json', json.dumps(manifest))
            data = buffer.getvalue()
        client = self.client([row(direct=not archive)])
        release = client.latest(1)
        release['asset']['size'] = len(data)
        sums = (hashlib.sha256(data).hexdigest() + '  ' + release['asset']['name'] + '\n').encode()
        client.fetch = Mock(side_effect=[sums, data])
        return client, release

    def test_direct_and_existing_zip_releases_are_verified(self):
        for archive in (False, True):
            client, release = self.setup_download(archive=archive)
            self.assertEqual(client.download(release, Mock()), image())

    def test_corrupt_asset_never_returns_firmware(self):
        client, release = self.setup_download()
        client.fetch.side_effect = [b'0' * 64 + b'  ' + release['asset']['name'].encode(), image()]
        with self.assertRaisesRegex(ReleaseError, 'checksum'): client.download(release, Mock())

    def test_wrong_target_release_version_or_development_is_rejected(self):
        for content in [image(2), image(version='0.1.0-alpha.3'), image(version='0.1.0-alpha.4-dev')]:
            client, release = self.setup_download(content=content)
            with self.assertRaises(ReleaseError): client.download(release, Mock())

    def test_missing_duplicate_or_github_digest_mismatch(self):
        client, release = self.setup_download()
        sums = hashlib.sha256(image()).hexdigest() + '  ' + release['asset']['name'] + '\n'
        for metadata in [b'', (sums + sums).encode()]:
            client.fetch.side_effect = [metadata]
            with self.assertRaises(ReleaseError): client.download(release, Mock())
        client, release = self.setup_download()
        release['asset']['digest'] = 'sha256:' + '0' * 64
        with self.assertRaises(ReleaseError): client.download(release, Mock())

    def test_download_size_and_redirect_boundaries(self):
        opener = MagicMock()
        opener.open.return_value.__enter__.return_value.headers = {'Content-Length': '999999999'}
        client = ReleaseClient(opener=opener)
        with self.assertRaisesRegex(ReleaseError, 'size'): client.fetch(API, 100)
        opener.open.return_value.__enter__.return_value.headers = {}
        opener.open.return_value.__enter__.return_value.read.return_value = b'x' * 101
        with self.assertRaisesRegex(ReleaseError, 'size'): client.fetch(API, 100)
        for url in ['http://github.com/a', 'https://evil.test/a', 'https://github.com@evil.test/a']:
            with self.assertRaises(ReleaseError): client.fetch(url, 100)
            with self.assertRaises(ReleaseError): _Redirects().redirect_request(None,None,302,'',{},url)


if __name__ == '__main__':
    unittest.main()
