# SPDX-License-Identifier: GPL-2.0-only
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nb4_storage_backup import backup_storage, verify_backup


class StorageBackupTests(unittest.TestCase):
    def test_complete_filesystem_backup_is_verified_and_never_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            radio = root / 'radio'
            for name in ('RADIO/radio.yml', 'MODELS/model01.yml', 'LOGS/race.csv',
                         'SOUNDS/en/custom.wav', 'THEMES/user/theme.yml'):
                path = radio / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
            destination = root / 'private-backup'
            result = backup_storage(radio, destination)
            self.assertEqual(len(result['files']), 5)
            self.assertFalse(result['raw_nor'])
            self.assertEqual(verify_backup(destination), result)
            with self.assertRaises(FileExistsError):
                backup_storage(radio, destination)
            (destination / 'data/MODELS/model01.yml').write_bytes(b'damaged')
            with self.assertRaisesRegex(RuntimeError, 'checksum'):
                verify_backup(destination)

    def test_symlinks_and_backup_inside_radio_are_rejected_before_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            radio = root / 'radio'
            radio.mkdir()
            with self.assertRaisesRegex(RuntimeError, 'on the computer'):
                backup_storage(radio, radio / 'backup')
            private = root / 'private.txt'
            private.write_bytes(b'do not copy through a link')
            try:
                (radio / 'link').symlink_to(private)
            except OSError:
                self.skipTest('Symbolic-link creation unavailable')
            with self.assertRaisesRegex(RuntimeError, 'Symbolic'):
                backup_storage(radio, root / 'backup')
            self.assertFalse((root / 'backup').exists())


if __name__ == '__main__':
    unittest.main()
