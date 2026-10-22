# SPDX-License-Identifier: GPL-2.0-only
"""The theme editor only rewrites the sections it owns, after a backup."""
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))

import nb4_editor_files as files

MODEL = '''semver: 2.12.5
header:
  name: "Drifter"
  bitmap: ""
timers:
  0:
    swtch: "NONE"
screenData:
  0:
    LayoutId: ApexTXRacing
    layoutData:
      zones:
        0:
          widgetName: "ApexSteering"
        1:
          widgetName: "ApexThrottle"
        2:
          widgetName: "ApexTimer"
        3:
          widgetName: "ApexStats"
  1:
    LayoutId: Layout2x1
    layoutData:
      zones:
        0:
          widgetName: "NB4Battery"
          widgetData:
            options:
              0:
                type: Signed
                value:
                  signedValue: 46
      options:
        0:
          type: Bool
          value:
            boolValue: 0
topbarData:
  zones:
    0:
      widgetName: "NB4Link"
view: 0
nb4ScreenVersion: 1
'''

RADIO = '''semver: 2.12.5
currModelFilename: "model01.yml"
selectedTheme: "ApexTX Dark"
nb4Accent: 0
nb4Orientation: 0
'''


class EditorFilesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name) / 'radio'
        (self.root / 'MODELS').mkdir(parents=True)
        (self.root / 'RADIO').mkdir()
        self.model = self.root / 'MODELS' / 'model01.yml'
        self.model.write_bytes(MODEL.replace('\n', '\r\n').encode())
        (self.root / 'RADIO' / 'radio.yml').write_bytes(RADIO.encode())
        self.backups = pathlib.Path(self.tmp.name) / 'backups'

    def tearDown(self):
        self.tmp.cleanup()

    def test_lists_models_with_names(self):
        models = files.list_models(self.root)
        self.assertEqual(models, [{'file': 'model01.yml', 'name': 'Drifter', 'editable': True, 'current': True}])

    def test_unchanged_screens_keep_the_file_identical(self):
        before = self.model.read_bytes()
        model = files.read_model(self.root, 'model01.yml')
        self.assertEqual(files.unquote(model['screenData']['1']['layoutData']['zones']['0']['widgetName']), 'NB4Battery')
        files.write_model(self.root, 'model01.yml', model['hash'], model['screenData'], self.backups)
        self.assertEqual(self.model.read_bytes(), before)

    def test_changes_only_the_screen_block_and_backs_up(self):
        model = files.read_model(self.root, 'model01.yml')
        screens = model['screenData']
        screens['0']['layoutData']['zones']['0']['widgetName'] = files.quote('NB4SteerWheel')
        result = files.write_model(self.root, 'model01.yml', model['hash'], screens, self.backups)
        text = self.model.read_bytes().decode()
        self.assertIn('widgetName: "NB4SteerWheel"', text)
        self.assertIn('\r\ntopbarData:\r\n  zones:', text)
        self.assertTrue(text.endswith('nb4ScreenVersion: 1\r\n'))
        self.assertEqual(pathlib.Path(result['backup']).read_bytes().decode(), MODEL.replace('\n', '\r\n'))

    def test_refuses_a_file_changed_on_the_radio(self):
        model = files.read_model(self.root, 'model01.yml')
        self.model.write_bytes(self.model.read_bytes() + b'# radio\r\n')
        with self.assertRaises(files.EditorFileError):
            files.write_model(self.root, 'model01.yml', model['hash'], model['screenData'], self.backups)

    def test_refuses_unmigrated_models_and_bad_values(self):
        self.model.write_bytes(MODEL.replace('nb4ScreenVersion: 1\n', '').encode())
        model = files.read_model(self.root, 'model01.yml')
        with self.assertRaises(files.EditorFileError):
            files.write_model(self.root, 'model01.yml', model['hash'], model['screenData'], self.backups)
        self.model.write_bytes(MODEL.encode())
        model = files.read_model(self.root, 'model01.yml')
        model['screenData']['0']['LayoutId'] = 'Bad\nInjected: 1'
        with self.assertRaises(files.EditorFileError):
            files.write_model(self.root, 'model01.yml', model['hash'], model['screenData'], self.backups)
        with self.assertRaises(files.EditorFileError):
            files.read_model(self.root, '../RADIO/radio.yml')

    def test_radio_settings(self):
        radio = files.read_radio(self.root)
        self.assertEqual(radio['settings']['selectedTheme'], 'ApexTX Dark')
        files.write_radio(self.root, radio['hash'], {'selectedTheme': 'Drift Neon', 'nb4LedMode': 2}, self.backups)
        text = (self.root / 'RADIO' / 'radio.yml').read_text()
        self.assertIn('selectedTheme: "Drift Neon"\n', text)
        self.assertTrue(text.endswith('nb4Orientation: 0\nnb4LedMode: 2\n'))
        radio = files.read_radio(self.root)
        with self.assertRaises(files.EditorFileError):
            files.write_radio(self.root, radio['hash'], {'currModelFilename': 'x.yml'}, self.backups)


if __name__ == '__main__':
    unittest.main()
