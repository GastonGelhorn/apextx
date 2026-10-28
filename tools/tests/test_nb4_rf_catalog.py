# SPDX-License-Identifier: GPL-2.0-only
"""RF package/inventory tests use synthetic data only; never FlySky binaries."""
import binascii
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import warnings
from unittest.mock import patch
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import nb4_rf_catalog as rf


def fixture(modify=None, corrupt=None):
    base = 0x50000000  # Arbitrary synthetic relocation, not a transmitter address.
    resource = bytearray(0x1000)
    struct.pack_into('<4H', resource, 0, 0x5a5a, 0, 1, 1)
    resource[64:80] = b'Firmware\0' + bytes(7)
    struct.pack_into('<3I', resource, 20, base + 64, 1, base + 128)
    struct.pack_into('<2I', resource, 128, 3, base + 0x800)
    images, offset = [], 256
    for index, (kind, product, version) in enumerate((
            ('module', 0x12340003, 0x10021),
            ('receiver', 0x12340004, 0x113),
            ('receiver', 0x12340006, 0x111))):
        # Test-owned byte sequences; nothing here is a firmware dump.
        payload = (b'ApexTX synthetic RF test data ' + bytes([index])) * 3
        payload += (binascii.crc_hqx(payload, 0xffff).to_bytes(2, 'little') if kind == 'module'
                    else zlib.crc32(payload).to_bytes(4, 'little'))
        if corrupt == index:
            payload = bytes([payload[0] ^ 1]) + payload[1:]
        resource[offset:offset + len(payload)] = payload
        struct.pack_into('<5I', resource, 0x800 + index * 20,
                         0x10000 + (kind == 'receiver'), product, version,
                         len(payload), base + offset)
        images.append(dict(kind=kind, product=product, version=version,
                           bytes=len(payload), sha256=rf.digest(payload), label=f'Test {index}'))
        offset += len(payload)
    if modify:
        modify(resource)
    # Minimal PE with one numeric RCDATA resource and one language.
    directory = bytearray(88)
    for offset, key, child in ((0, 10, 0x80000018),
                               (24, 901, 0x80000030), (48, 1033, 72)):
        struct.pack_into('<H', directory, offset + 14, 1)
        struct.pack_into('<2I', directory, offset + 16, key, child)
    struct.pack_into('<4I', directory, 72, 0x1000 + 88, len(resource), 0, 0)
    raw = bytes(directory + resource)
    exe = bytearray(512)
    exe[:2] = b'MZ'; struct.pack_into('<I', exe, 60, 128)
    exe[128:132] = b'PE\0\0'
    struct.pack_into('<2H', exe, 132, 0x14c, 1)
    struct.pack_into('<H', exe, 148, 224)
    struct.pack_into('<H', exe, 152, 0x10b)
    struct.pack_into('<I', exe, 152 + 92, 16)
    struct.pack_into('<2I', exe, 152 + 96 + 16, 0x1000, len(raw))
    struct.pack_into('<4I', exe, 376 + 8, len(raw), 0x1000, len(raw), 512)
    source = bytes(exe) + raw
    profile = dict(id='synthetic-nb4', radio='nb4', version='test',
                   url='https://example.invalid/test-owned', sha256=rf.digest(source),
                   resource_sha256=rf.digest(resource), images=tuple(images))
    return source, profile


def changed_bundle(raw, mutate=None, extra=None):
    result = io.BytesIO()
    with warnings.catch_warnings(), zipfile.ZipFile(io.BytesIO(raw)) as source, zipfile.ZipFile(result, 'w') as target:
        warnings.simplefilter('ignore', UserWarning)  # Deliberately duplicate a ZIP member.
        for item in source.infolist():
            content = source.read(item)
            target.writestr(item, mutate(item.filename, content) if mutate else content)
        if extra:
            target.writestr(*extra)
    return result.getvalue()


class RfCatalog(unittest.TestCase):
    def setUp(self):
        self.source, self.profile = fixture()
        self.profiles = patch.object(rf, 'PROFILES', (self.profile,))
        self.profiles.start(); self.addCleanup(self.profiles.stop)

    def bundle(self):
        description, images = rf.import_source(self.source)
        return rf.bundle_bytes(description, images)

    def test_known_source_extracts_only_the_three_pinned_complete_images(self):
        description, images = rf.import_source(self.source)
        self.assertEqual(len(images), 3)
        self.assertFalse(description['rf_write_supported'])
        self.assertEqual(rf.verify_bundle(self.bundle()), (description, images))
        self.assertEqual(self.bundle(), self.bundle())
        self.assertEqual(set(images), {rf.image_name(i) for i in self.profile['images']})

    def test_changed_or_unknown_source_never_reaches_resource_parsing(self):
        for data in (b'unknown', self.source[:-1] + b'1'):
            with self.assertRaises(rf.CatalogError), patch.object(rf, 'resource_from_pe') as parse:
                rf.import_source(data)
            parse.assert_not_called()

    def test_source_and_resource_hashes_are_separate_gates(self):
        modified = self.source[:-1] + bytes([self.source[-1] ^ 1])
        self.profile['sha256'] = rf.digest(modified)
        with self.assertRaisesRegex(rf.CatalogError, 'resource does not match'):
            rf.import_source(modified)

    def test_crc_checks_are_not_replaced_by_sha256_checks(self):
        for index in range(3):
            source, profile = fixture(corrupt=index)
            with patch.object(rf, 'PROFILES', (profile,)), self.assertRaisesRegex(rf.CatalogError, 'checksum'):
                rf.import_source(source)

    def test_bad_catalog_ranges_versions_and_duplicate_products_are_rejected(self):
        changes = (
            lambda b: struct.pack_into('<I', b, 20, 0),
            lambda b: struct.pack_into('<I', b, 128, 65),
            lambda b: struct.pack_into('<I', b, 0x800 + 16, 0xc1000000),
            lambda b: struct.pack_into('<I', b, 0x800 + 12, rf.MAX_IMAGE + 1),
            lambda b: struct.pack_into('<I', b, 0x800 + 8, 0x10022),
            lambda b: struct.pack_into('<I', b, 0x800 + 20 + 16, 0x50000100),
            lambda b: struct.pack_into('<I', b, 0x800 + 40 + 4, 0x12340004),
        )
        for change in changes:
            source, profile = fixture(modify=change)
            with patch.object(rf, 'PROFILES', (profile,)), self.assertRaises(rf.CatalogError):
                rf.import_source(source)

    def test_truncated_pe_and_ambiguous_resources_are_rejected(self):
        for size in (0, 2, 64, 130, 160, 400, 520, len(self.source) - 1):
            with self.subTest(size=size), self.assertRaises(rf.CatalogError):
                rf.resource_from_pe(self.source[:size])
        for offset, value in ((60, 0xffffffff), (134, 97), (152, 0),
                              (512 + 48 + 14, 2), (512 + 72 + 12, 1)):
            data = bytearray(self.source)
            struct.pack_into('<I', data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(rf.CatalogError):
                rf.resource_from_pe(data)

    def test_forged_manifests_cannot_change_target_versions_or_enable_writing(self):
        for change in (dict(radio='nb4plus'), dict(rf_write_supported=True),
                       dict(rf_write_supported=0), dict(profile='unknown')):
            def mutate(name, data):
                if name != 'APEXTX-RF.json': return data
                document = json.loads(data); document.update(change)
                return json.dumps(document).encode()
            with self.subTest(change=change), self.assertRaises(rf.CatalogError):
                rf.verify_bundle(changed_bundle(self.bundle(), mutate))

    def test_recomputed_zip_crc_and_manifest_sha_cannot_authorize_replaced_image(self):
        target = rf.image_name(self.profile['images'][1])
        def mutate(name, data):
            if name == target: return bytes([data[0] ^ 1]) + data[1:]
            return data
        with self.assertRaises(rf.CatalogError):
            rf.verify_bundle(changed_bundle(self.bundle(), mutate))
        def forge(name, data):
            if name == target: return mutate(name, data)
            if name == 'APEXTX-RF.json':
                document = json.loads(data); document['images'][1]['sha256'] = '0' * 64
                return json.dumps(document).encode()
            return data
        with self.assertRaises(rf.CatalogError):
            rf.verify_bundle(changed_bundle(self.bundle(), forge))

    def test_extra_unsafe_duplicate_symlink_or_oversized_entries_are_rejected(self):
        symlink = zipfile.ZipInfo('symlink'); symlink.external_attr = 0o120777 << 16
        for extra in (('../escape', b'x'), ('/absolute', b'x'), ('C:\\escape', b'x'),
                      ('APEXTX-RF.json', b'{}'), (symlink, b'/tmp'),
                      ('bomb', b'0' * (rf.MAX_BUNDLE + 1))):
            with self.subTest(extra=str(extra[0])), self.assertRaises(rf.CatalogError):
                rf.verify_bundle(changed_bundle(self.bundle(), extra=extra))

    def test_manifest_duplicate_keys_and_non_object_json_are_rejected(self):
        for data in (b'[]', b'{"profile":"synthetic-nb4","profile":"synthetic-nb4"}'):
            with self.assertRaises(rf.CatalogError):
                rf.verify_bundle(changed_bundle(self.bundle(), lambda n, b: data if n == 'APEXTX-RF.json' else b))

    def test_prepare_never_overwrites_the_input_or_existing_output(self):
        with tempfile.TemporaryDirectory() as folder:
            source, output = Path(folder) / 'source.exe', Path(folder) / 'rf.zip'
            source.write_bytes(self.source)
            rf.prepare(source, output)
            original = output.read_bytes()
            with self.assertRaises(FileExistsError): rf.prepare(source, output)
            with self.assertRaises(FileExistsError): rf.prepare(source, source)
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(source.read_bytes(), self.source)
            rf.verify_bundle(original)

    def test_post_bind_assessment_never_confuses_available_required_or_downgrade(self):
        description = rf.manifest(self.profile)
        installed = dict(connected=True, valid=True, product=0x12340004, firmware=0x112)
        for version, expected in ((0x112, 'available'), (0x113, 'current'), (0x114, 'ahead')):
            result = rf.assess_receiver(description, dict(installed, firmware=version))
            self.assertEqual(result['state'], expected)
            self.assertFalse(result['required']); self.assertFalse(result['write_supported'])
        for change in (dict(connected=False), dict(valid=False), dict(firmware=None),
                       dict(firmware=True), dict(firmware=-1), dict(firmware=0x10000),
                       dict(product=0), dict(product=True), dict(firmware=275.0)):
            self.assertEqual(rf.assess_receiver(description, dict(installed, **change))['state'], 'unknown')

    def test_cli_inspection_preparation_and_assessment_never_import_usb(self):
        spec = importlib.util.spec_from_file_location('rf_cli', ROOT / 'tools/apextx-rf-package.py')
        cli = importlib.util.module_from_spec(spec); spec.loader.exec_module(cli)
        with tempfile.TemporaryDirectory() as folder:
            source, output = Path(folder) / 'official.exe', Path(folder) / 'rf.zip'
            source.write_bytes(self.source)
            for args in ([str(source)], [str(source), '--output', str(output)], [str(output), '--check']):
                stream = io.StringIO()
                with patch.object(sys, 'argv', ['rf-cli', *args]), patch('sys.stdout', stream):
                    cli.main()
                self.assertFalse(json.loads(stream.getvalue())['device_writes'])
            self.assertNotIn('usb', cli.__dict__)

    def test_rf_bundle_is_not_a_radio_image_and_cannot_enter_normal_update(self):
        from nb4_update_image import update_package
        with self.assertRaises(ValueError): update_package(self.bundle())
        with self.assertRaises(ValueError): update_package(self.source)

    def test_shared_flasher_preparation_never_sets_up_usb_even_on_failure(self):
        spec = importlib.util.spec_from_file_location('integrated_flasher', ROOT / 'tools/apextx-updater.py')
        flasher = importlib.util.module_from_spec(spec); spec.loader.exec_module(flasher)
        with tempfile.TemporaryDirectory() as folder:
            source, output = Path(folder) / 'official.exe', Path(folder) / 'catalog.zip'
            source.write_bytes(self.source)
            for args in (['--rf-source', str(source)],
                         ['--rf-source', str(source), '--rf-output', str(output)],
                         ['--rf-bundle', str(output)]):
                stream = io.StringIO()
                with patch.object(sys, 'argv', ['flasher', *args]), patch('sys.stdout', stream), \
                        patch.object(flasher, '_enable_bundled_libusb') as setup, \
                        patch.object(flasher, 'discover') as discover, \
                        patch.object(flasher, 'run_update') as update:
                    flasher.main()
                setup.assert_not_called(); discover.assert_not_called(); update.assert_not_called()
                self.assertFalse(json.loads(stream.getvalue())['device_writes'])
            source.write_bytes(b'unknown')
            with patch.object(sys, 'argv', ['flasher', '--rf-source', str(source)]), \
                    patch('sys.stderr', io.StringIO()), \
                    patch.object(flasher, '_enable_bundled_libusb') as setup, \
                    self.assertRaises(SystemExit) as failure:
                flasher.main()
            self.assertEqual(failure.exception.code, 1); setup.assert_not_called()

    def test_shared_flasher_rejects_mixed_radio_and_catalog_operations(self):
        spec = importlib.util.spec_from_file_location('mixed_flasher', ROOT / 'tools/apextx-updater.py')
        flasher = importlib.util.module_from_spec(spec); spec.loader.exec_module(flasher)
        for args in (['fake.bin', '--rf-source', 'official.exe'],
                     ['--rf-source', 'official.exe', '--check'],
                     ['--rf-bundle', 'rf.zip', '--rf-output', 'output.zip'],
                     ['--rf-output', 'output.zip']):
            with patch.object(sys, 'argv', ['flasher', *args]), patch('sys.stderr', io.StringIO()), \
                    patch.object(flasher, '_enable_bundled_libusb') as setup, self.assertRaises(SystemExit):
                flasher.main()
            setup.assert_not_called()


if __name__ == '__main__':
    unittest.main()
