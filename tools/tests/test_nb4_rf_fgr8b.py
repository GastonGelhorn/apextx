# SPDX-License-Identifier: GPL-2.0-only
"""FGr8B catalog tests contain only metadata and synthetic, test-owned bytes."""
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import nb4_rf_catalog as rf
from tools.tests.test_nb4_rf_catalog import fixture, changed_bundle


class Fgr8bPins(unittest.TestCase):
    def test_source_generation_selects_the_exact_variant(self):
        for profile in rf.PROFILES:
            with self.subTest(profile=profile['id']):
                entries = [image for image in profile['images'] if image['product'] == 0x10107]
                self.assertEqual(len(entries), 1)
                expected = rf.FGR8B_NB4 if profile['radio'] == 'nb4' else rf.FGR8B_NB4PLUS
                self.assertEqual(entries[0], expected)
                self.assertEqual(len(profile['images']), 8 if profile['radio'] == 'nb4' else 7)
                self.assertFalse(rf.manifest(profile)['rf_write_supported'])

    def test_metadata_matches_the_independent_report_and_distinguishes_old_image(self):
        self.assertEqual((rf.FGR8B_NB4PLUS['version'], rf.FGR8B_NB4PLUS['bytes'], rf.FGR8B_NB4PLUS['sha256']),
            (0x117, 88320, 'f0f25179d309f90d2d33611cbbacae200457c058aee3d2255b558e5eaaa5a37a'))
        self.assertEqual((rf.FGR8B_NB4['version'], rf.FGR8B_NB4['bytes'], rf.FGR8B_NB4['sha256']),
            (0x114, 64464, 'a3f55ca1b90d0e82b1302ea3004600431d79b62fc9231ca54af4ebbd10aeb8b5'))
        self.assertNotEqual(rf.FGR8B_NB4['sha256'], rf.FGR8B_NB4PLUS['sha256'])
        self.assertEqual(rf.IMAGES[:3], rf.COMMON_IMAGES, 'Existing engine indices must remain stable')

    def test_only_the_nb4plus_image_carries_a_provisional_product_only_rule(self):
        candidates = {index for index, image in enumerate(rf.IMAGES) if image['product'] == 0x10107}
        self.assertEqual(candidates, {3, 8})
        rules = [rule for rule in rf.UPDATE_HARDWARE if rule['image'] >= 3]
        self.assertEqual([(rule['target'], rule['image']) for rule in rules], [(2, 8)])
        self.assertTrue(rules[0]['provisional'])
        self.assertEqual({rules[0][key] for key in ('hardware', 'rf_hardware', 'bootloader', 'rf_library')},
                         {rf.ANY_IDENTITY})
        self.assertFalse(any(rf.ANY_IDENTITY in (rule['hardware'], rule['rf_hardware'], rule['bootloader'],
                                                  rule['rf_library']) for rule in rf.UPDATE_HARDWARE if rule['image'] < 3))
        self.assertIn('rfUpdateAnyIdentity = 0xffffffff', rf.firmware_catalog_header())
        self.assertIn('{2, 8, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, true, true}', rf.firmware_catalog_header())
        self.assertEqual(len({(entry['kind'], entry['product'], entry['version']) for entry in rf.IMAGES}), len(rf.IMAGES))

    def test_current_ahead_available_unknown_and_disconnected_never_authorize_writes(self):
        for profile in rf.PROFILES:
            offered = next(image for image in profile['images'] if image['product'] == 0x10107)['version']
            for installed, state in ((offered, 'current'), (offered - 1, 'available'), (offered + 1, 'ahead')):
                result = rf.assess_receiver(rf.manifest(profile), dict(product=0x10107, firmware=installed, connected=True, valid=True))
                self.assertEqual(result['state'], state)
                self.assertFalse(result['required'])
                self.assertFalse(result['write_supported'])
            for invalid in (dict(connected=False), dict(valid=False), dict(firmware=None), dict(product=0x101ff)):
                inventory = dict(product=0x10107, firmware=0x117, connected=True, valid=True)
                inventory.update(invalid)
                self.assertEqual(rf.assess_receiver(rf.manifest(profile), inventory)['state'], 'unknown')

    def test_newer_receiver_candidate_can_be_assessed_for_nb4_from_plus_source(self):
        # Source transmitter is provenance, not an RF-hardware compatibility
        # certificate or a reason to discard a newer receiver candidate.
        original_nb4 = dict(target='nb4', product=0x10107, firmware=0x114, connected=True, valid=True)
        for profile in rf.PROFILES:
            if profile['radio'] != 'nb4plus':
                continue
            result = rf.assess_receiver(rf.manifest(profile), original_nb4)
            self.assertEqual((result['state'], result['available']), ('available', 0x117))
            self.assertFalse(result['required'])
            self.assertFalse(result['write_supported'])

    def test_reported_0x117_is_ahead_of_nb4_package_not_a_downgrade_offer(self):
        for profile in rf.PROFILES:
            result = rf.assess_receiver(rf.manifest(profile), dict(product=0x10107, firmware=0x117, connected=True, valid=True))
            self.assertEqual(result['state'], 'ahead' if profile['radio'] == 'nb4' else 'current')
            self.assertFalse(result['required'])
            self.assertFalse(result['write_supported'])


class Fgr8bSyntheticImport(unittest.TestCase):
    def source(self, corrupt_crc=False, wrong_version=False):
        # Reuse the project-owned PE/container fixture. Relabel its third
        # synthetic receiver; never store an official receiver image in tests.
        source, profile = fixture()
        resource = bytearray(rf.resource_from_pe(source))
        item = dict(profile['images'][2], product=0x10107, version=0x117, label='Synthetic FGr8B')
        table = 0x800 + 2 * 20
        struct.pack_into('<II', resource, table + 4, item['product'], item['version'] if not wrong_version else 0x114)
        address = struct.unpack_from('<I', resource, table + 16)[0] - 0x50000000
        if corrupt_crc:
            resource[address] ^= 1
            item['sha256'] = rf.digest(resource[address:address + item['bytes']])
        images = list(profile['images']); images[2] = item
        profile = dict(profile, images=tuple(images), resource_sha256=rf.digest(resource))
        source = source[:-len(resource)] + resource
        profile['sha256'] = rf.digest(source)
        return source, profile

    def test_fgr8b_round_trip_uses_exact_source_resource_payload_and_crc(self):
        source, profile = self.source()
        with patch.object(rf, 'PROFILES', (profile,)):
            description, images = rf.import_source(source)
            raw = rf.bundle_bytes(description, images)
            self.assertEqual(rf.verify_bundle(raw), (description, images))
            name = rf.image_name(profile['images'][2])
            self.assertIn('receiver-00010107-00000117', name)
            self.assertIn(name, images)

    def test_relabelled_version_and_rehashed_bad_crc_are_rejected(self):
        for invalid in (dict(wrong_version=True), dict(corrupt_crc=True)):
            source, profile = self.source(**invalid)
            with patch.object(rf, 'PROFILES', (profile,)), self.assertRaises(rf.CatalogError):
                rf.import_source(source)

    def test_an_old_variant_cannot_be_relabelled_as_the_new_one(self):
        source, profile = self.source()
        with patch.object(rf, 'PROFILES', (profile,)):
            description, images = rf.import_source(source)
            raw = rf.bundle_bytes(description, images)
            def relabel(name, data):
                if name == 'APEXTX-RF.json':
                    doc = json.loads(data)
                    doc['images'][2]['version'] = 0x114
                    return json.dumps(doc).encode()
                return data
            with self.assertRaises(rf.CatalogError):
                rf.verify_bundle(changed_bundle(raw, relabel))

    def test_payload_replacement_cannot_be_authorized_by_a_rewritten_manifest(self):
        source, profile = self.source()
        with patch.object(rf, 'PROFILES', (profile,)):
            description, images = rf.import_source(source)
            raw = rf.bundle_bytes(description, images)
            target = rf.image_name(profile['images'][2])
            altered = bytes([images[target][0] ^ 1]) + images[target][1:]
            def replace(name, data):
                if name == target:
                    return altered
                if name == 'APEXTX-RF.json':
                    doc = json.loads(data)
                    doc['images'][2]['sha256'] = rf.digest(altered)
                    return json.dumps(doc).encode()
                return data
            with self.assertRaises(rf.CatalogError):
                rf.verify_bundle(changed_bundle(raw, replace))
