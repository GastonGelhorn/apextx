# SPDX-License-Identifier: GPL-2.0-only
import pathlib
import sys
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from nb4_preview import FirmwarePreview


class PreviewLifecycle(unittest.TestCase):
    def setUp(self):
        self.preview = FirmwarePreview()
        self.process = Mock()
        self.process.poll.return_value = 0
        self.preview.process = self.process
        self.preview.frame = (1, 320, 480, b'', [], 0)
        self.connection = Mock()
        self.connection.poll.return_value = True

    def tearDown(self):
        self.preview.close()

    def test_worker_eof_exposes_a_nonempty_error_and_removes_frame(self):
        self.connection.recv_bytes.side_effect = EOFError()
        self.preview._read(self.process, self.connection)
        self.assertTrue(self.preview.error)
        self.assertIsNone(self.preview.latest(0))

    def test_truncated_frame_is_reported_instead_of_killing_reader_silently(self):
        self.connection.recv_bytes.return_value = b'NB4F'
        self.preview._read(self.process, self.connection)
        self.assertTrue(self.preview.error)
        self.assertIsNone(self.preview.latest(0))

    def test_worker_timeout_removes_last_frame(self):
        self.connection.poll.return_value = False
        self.preview._read(self.process, self.connection)
        self.assertIn('timed out', self.preview.error)
        self.assertIsNone(self.preview.latest(0))

    def test_old_worker_eof_does_not_clear_new_worker_frame(self):
        self.connection.recv_bytes.side_effect = EOFError()
        self.preview._read(Mock(), self.connection)
        self.assertIsNotNone(self.preview.latest(0))
        self.assertIsNone(self.preview.error)

    def test_old_worker_metadata_is_discarded(self):
        self.connection.recv_bytes.return_value = b'NB4M{"firmware":"old"}'
        self.preview._read(Mock(), self.connection)
        self.assertIsNone(self.preview.catalog)


if __name__ == '__main__':
    unittest.main()
