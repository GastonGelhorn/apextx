# SPDX-License-Identifier: GPL-2.0-only
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from nb4_preview import FirmwarePreview

class PreviewResources(unittest.TestCase):
    def test_factory_model_with_yaml_sequences_stages_without_a_radio(self):
        preview=FirmwarePreview()
        try:
            storage=preview._stage({},None)
            self.assertTrue((storage/'RADIO/radio.yml').is_file())
            self.assertTrue((storage/'MODELS/model1.yml').is_file())
        finally:preview.close()
    def test_referenced_media_are_copies_and_parent_paths_are_refused(self):
        preview=FirmwarePreview()
        try:
            with tempfile.TemporaryDirectory() as directory:
                root=pathlib.Path(directory).resolve();source=root/'source';dest=root/'dest';dest.mkdir()
                (source/'IMAGES').mkdir(parents=True);original=source/'IMAGES/car.png';original.write_bytes(b'original')
                preview._assets(source,dest,b'header:\n   bitmap: "car"\nitems:\n   -\n      file: "/IMAGES/car.png"\n')
                (dest/'IMAGES/car.png').write_bytes(b'private change')
                self.assertEqual(original.read_bytes(),b'original')
                with self.assertRaises(ValueError):preview._assets(source,dest,b'file: "../outside.png"\n')
        finally:preview.close()

if __name__=='__main__':unittest.main()
