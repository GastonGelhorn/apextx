# SPDX-License-Identifier: GPL-2.0-only
"""Check the package renderer probe without a display, USB or a Linux host."""
import importlib.util
import pathlib
import sys
import types
import unittest
from unittest.mock import Mock, patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('flasher_linux_runtime', ROOT / 'tools/apextx-updater.py')
flasher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flasher)


class LinuxRenderer(unittest.TestCase):
    def test_packaged_backend_is_loaded_without_starting_a_window(self):
        backend = types.SimpleNamespace(renderer='gtkwebkit2',
            gtk=types.SimpleNamespace(get_major_version=lambda: 3),
            webkit=types.SimpleNamespace(get_major_version=lambda: 2),
            setup_app=Mock(), create_window=Mock())
        with patch.object(sys, 'platform', 'linux'), \
                patch('importlib.import_module', return_value=backend) as load:
            self.assertEqual(flasher.native_renderer_self_test(), 'gtkwebkit2')
        load.assert_called_once_with('webview.platforms.gtk')
        backend.setup_app.assert_not_called()
        backend.create_window.assert_not_called()

    def test_missing_binding_or_host_runtime_fails_the_package_check(self):
        for error in (ModuleNotFoundError("No module named 'gi'"),
                      ValueError('Namespace WebKit2 not available'),
                      OSError('libwebkit2gtk cannot be loaded')):
            with self.subTest(error=error), patch.object(sys, 'platform', 'linux'), \
                    patch('importlib.import_module', side_effect=error):
                with self.assertRaisesRegex(RuntimeError, 'GTK/WebKit renderer') as raised:
                    flasher.native_renderer_self_test()
                self.assertIs(raised.exception.__cause__, error)

    def test_other_platforms_do_not_require_linux_bindings(self):
        for platform in ('darwin', 'win32'):
            with self.subTest(platform=platform), patch.object(sys, 'platform', platform), \
                    patch('importlib.import_module') as load:
                self.assertIsNone(flasher.native_renderer_self_test())
                load.assert_not_called()


if __name__ == '__main__':
    unittest.main()
