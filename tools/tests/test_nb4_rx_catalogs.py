# SPDX-License-Identifier: GPL-2.0-only
"""Source-independent receiver-candidate selection; no USB or vendor bytes."""
import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import nb4_rf_catalog as rf


class ReceiverCatalogs(unittest.TestCase):
    def descriptions(self):
        return [rf.manifest(profile) for profile in rf.SOURCE_PROFILES]

    def inventory(self, product, firmware, **extra):
        return dict(product=product, firmware=firmware, target='nb4', connected=True, valid=True, **extra)

    def test_latest_verified_variant_is_independent_of_source_transmitter(self):
        versions = {0x12340004:0x113, 0x12340006:0x111, 0x10104:0x108,
                    0x1010e:0x117, 0x10107:0x117, 0x10108:0x117, 0x1010b:0x108}
        for product, version in versions.items():
            for installed, state in ((version - 1,'available'), (version,'current'), (version + 1,'ahead')):
                with self.subTest(product=hex(product), installed=installed):
                    result = rf.assess_receiver_catalogs(self.descriptions(), self.inventory(product, installed))
                    self.assertEqual((result['state'],result['available']), (state,version))
                    self.assertFalse(result['required'])
                    self.assertFalse(result['write_supported'])
                    self.assertFalse(result['compatibility_verified'])
                    self.assertEqual(result['image']['product'],product)
                    self.assertEqual(result['image']['version'],version)
        result = rf.assess_receiver_catalogs(self.descriptions(),self.inventory(0x10107,0x114))
        self.assertEqual({row['radio'] for row in result['source_profiles']},{'nb4plus'})

    def test_all_existing_module_and_receiver_indices_remain_stable(self):
        self.assertEqual(rf.IMAGES[:3],rf.COMMON_IMAGES)
        self.assertEqual([i['product'] for i in rf.COMMON_IMAGES],[0x12340003,0x12340004,0x12340006])
        provisional=[(target,image) for target in (1,2) for image in (7,8,9,10,11)]
        self.assertEqual([(rule['target'],rule['image']) for rule in rf.UPDATE_HARDWARE if rule['image'] >= 3],provisional)
        self.assertEqual([rf.IMAGES[i]['product'] for i in (7,8,9,10,11)],[0x1010b,0x10107,0x10104,0x1010e,0x10108])

    def test_each_profile_has_one_payload_per_product_and_only_verified_images(self):
        accepted={(i['product'],i['version'],i['sha256']) for i in rf.IMAGES}
        for profile in rf.PROFILES:
            images=profile['images']
            self.assertEqual(len({i['product'] for i in images}),len(images))
            self.assertTrue(all((i['product'],i['version'],i['sha256']) in accepted for i in images))
            if profile.get('sources'):
                self.assertEqual(images,rf.COMMON_IMAGES+rf.LATEST_RECEIVER_IMAGES)
            else:
                self.assertEqual(len(images),8 if profile['radio']=='nb4' else 7)

    def test_merged_profiles_carry_the_latest_verified_image_of_every_product_for_both_radios(self):
        merged=[p for p in rf.PROFILES if p.get('sources')]
        self.assertEqual([(p['id'],p['radio']) for p in merged],[('nb4-rf-2','nb4'),('nb4plus-rf-2','nb4plus')])
        latest={}
        for image in rf.IMAGES[3:]:  # receivers pinned from the packages; indices 0-2 are common to both
            latest[image['product']]=max(latest.get(image['product'],0),image['version'])
        self.assertEqual({i['product']:i['version'] for i in rf.LATEST_RECEIVER_IMAGES},latest)
        for profile in merged:
            self.assertEqual(set(profile['sources']),{'nb4-rf-1b','nb4plus-rf-1b'})
            description=rf.manifest(profile)
            self.assertEqual(description['input_sha256'],[rf.source_profile(s)['sha256'] for s in profile['sources']])
            self.assertFalse(description['rf_write_supported'])
            for product,version in latest.items():
                self.assertEqual(rf.assess_receiver(description,self.inventory(product,version-1))['state'],'available')
                self.assertEqual(rf.assess_receiver(description,self.inventory(product,version))['state'],'current')
        # The provisional rules cover exactly the merged receiver images.
        ruled={rule['image'] for rule in rf.UPDATE_HARDWARE if rule['image']>=3}
        self.assertEqual(ruled,{rf.IMAGES.index(i) for i in rf.LATEST_RECEIVER_IMAGES})

    def test_newer_gyro_payload_is_recorded_but_not_exported_or_selected(self):
        self.assertEqual(len(rf.UNVERIFIED_RECEIVER_IMAGES),1)
        deferred=rf.UNVERIFIED_RECEIVER_IMAGES[0]
        self.assertEqual((deferred['product'],deferred['version'],deferred['reason']),(0x1010b,0x109,'crc32-mismatch'))
        self.assertNotIn(deferred,rf.IMAGES)
        self.assertTrue(all(deferred not in profile['images'] for profile in rf.PROFILES))
        result=rf.assess_receiver_catalogs(self.descriptions(),self.inventory(0x1010b,0x108))
        self.assertEqual(result['available'],0x108)
        self.assertEqual(result['known_unverified'],[dict(version=0x109,source_radio='nb4plus',reason='crc32-mismatch')])
        self.assertFalse(result['write_supported'])
        plus=[rf.manifest(p) for p in rf.SOURCE_PROFILES if p['radio']=='nb4plus']
        self.assertEqual(rf.assess_receiver_catalogs(plus,self.inventory(0x1010b,0x108))['state'],'unknown')

    def test_empty_stale_disconnected_invalid_or_forged_metadata_stays_unknown(self):
        inventory=self.inventory(0x10107,0x114)
        for descriptions in ([],None,[{}],self.descriptions()*2):
            self.assertEqual(rf.assess_receiver_catalogs(descriptions,inventory)['state'],'unknown')
        for change in (dict(connected=False),dict(valid=False),dict(fresh=False),dict(firmware=None),
                       dict(firmware=True),dict(firmware=0x10000),dict(product=True),dict(product=0xdead)):
            self.assertEqual(rf.assess_receiver_catalogs(self.descriptions(),dict(inventory,**change))['state'],'unknown')
        forged=self.descriptions()
        forged[0]['images'][0]['sha256']='0'*64
        self.assertEqual(rf.assess_receiver_catalogs(forged,inventory)['state'],'unknown')

    def test_identical_pins_from_regional_sources_are_deduplicated(self):
        result=rf.assess_receiver_catalogs(self.descriptions(),self.inventory(0x12340004,0x112))
        self.assertEqual(len(result['source_profiles']),4)
        self.assertEqual(result['image']['sha256'],rf.COMMON_IMAGES[1]['sha256'])

    def test_same_product_and_version_with_different_payloads_is_ambiguous(self):
        profile=copy.deepcopy(rf.PROFILES[0])
        conflict=copy.deepcopy(profile)
        conflict['id']='synthetic-conflicting-pin'
        for entry in conflict['images']:
            if entry['product']==0x10107:entry['sha256']='0'*64
        with patch.object(rf,'PROFILES',(profile,conflict)):
            result=rf.assess_receiver_catalogs([rf.manifest(profile),rf.manifest(conflict)],self.inventory(0x10107,0x113))
        self.assertEqual(result['state'],'ambiguous')
        self.assertFalse(result['write_supported'])
        self.assertNotIn('image',result)
