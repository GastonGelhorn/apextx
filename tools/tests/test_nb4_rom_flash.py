# SPDX-License-Identifier: GPL-2.0-only
"""Safety gates for the Windows full-image ROM DFU flasher."""
import io
import json
import pathlib
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))

import nb4_rom_flash as rom_flash
import nb4_update_image as image_format


def application(hardware=b'nb4'):
    app = bytearray(b'\x55' * 8192)
    struct.pack_into('<II', app, 0, 0x1000fff0, 0x08020101)
    app[32:46] = b'apextx-nb4-1.0'
    marker = b'APXHW:' + hardware + b'\x00'
    app[64:64 + len(marker)] = marker
    return app


def full_image(hardware=b'nb4', auto_start=False):
    bootloader = bytearray(b'\xff' * rom_flash.BOOT_SIZE)
    struct.pack_into('<II', bootloader, 0, 0x20020000, 0x08000101)
    if auto_start:
        bootloader[64:72] = rom_flash.ROM_START_MAGIC
        struct.pack_into('<II', bootloader, 72, 0x20020000, 0x08000301)
    return image_format.finalize(bytes(bootloader) + application(hardware))


def listing(serial='ABC123', size='04*016Kg,01*064Kg,07*128Kg,'
                                    '04*016Kg,01*064Kg,07*128Kg'):
    return (f'Found DFU: [0483:df11] ver=2200, devnum=1, cfg=1, intf=0, '
            f'path="1-1", alt=0, name="@Internal Flash /0x08000000/{size}", '
            f'serial="{serial}"\n')


class Images(unittest.TestCase):
    def test_optional_resume_vector_is_bounded_to_the_bootloader(self):
        self.assertIsNone(rom_flash.automatic_start_address(full_image()))
        self.assertEqual(rom_flash.automatic_start_address(full_image(auto_start=True)),
                         rom_flash.BOOT_ADDRESS + 72)
        for position, value in ((72, 0), (76, 0x08000300), (76, 0x08020101), (76, 0x08000101)):
            with self.subTest(position=position, value=value):
                image = bytearray(full_image(auto_start=True))
                struct.pack_into('<I', image, position, value)
                with self.assertRaises(rom_flash.RomFlashError):
                    rom_flash.automatic_start_address(image)

    def test_only_exact_manifested_apextx_images_are_accepted(self):
        for hardware, variant in ((b'nb4', 'NB4'), (b'nb4plus', 'NB4+')):
            with self.subTest(hardware=hardware):
                image = full_image(hardware)
                self.assertEqual(rom_flash.validate_full_image(image), variant)
                self.assertEqual(rom_flash.image_variant(image), variant)

        with self.assertRaises(rom_flash.RomFlashError):
            rom_flash.validate_full_image(full_image()[:rom_flash.BOOT_SIZE + 8])

        stock_like = bytearray(b'\xff' * rom_flash.FLASH_SIZE)
        struct.pack_into('<II', stock_like, 0, 0x20020000, 0x08000101)
        struct.pack_into('<II', stock_like, rom_flash.BOOT_SIZE,
                         0x20020000, 0x08020101)
        with self.assertRaises(rom_flash.RomFlashError):
            rom_flash.validate_full_image(stock_like)

    def test_corrupt_application_is_rejected(self):
        image = bytearray(full_image())
        image[rom_flash.BOOT_SIZE + 512] ^= 1
        with self.assertRaises(rom_flash.RomFlashError):
            rom_flash.validate_full_image(image)

    def test_corrupt_bootloader_is_rejected(self):
        image = bytearray(full_image())
        image[512] ^= 1
        with self.assertRaises(rom_flash.RomFlashError):
            rom_flash.validate_full_image(image)
        with self.assertRaises(ValueError):
            image_format.finalize(image)


class Discovery(unittest.TestCase):
    def discover(self, output, expected=None):
        result = SimpleNamespace(stdout=output, returncode=0)
        with patch.object(rom_flash, 'dfu_util_path', return_value='dfu-util'), \
                patch.object(rom_flash, '_run', return_value=result):
            return rom_flash.discover_rom_dfu(expected)

    def test_exact_two_mib_internal_flash_is_required(self):
        self.assertEqual(self.discover(listing()), 'ABC123')
        self.assertEqual(self.discover(listing().replace('@Internal Flash /',
                                                         '@Internal Flash  /')), 'ABC123')
        with self.assertRaises(rom_flash.RomFlashError):
            self.discover(listing(size='04*016Kg,01*064Kg,07*128Kg'))
        with self.assertRaises(rom_flash.RomFlashError):
            self.discover('Found DFU: [0483:df11] alt=0, '
                          'name="@Other device /0x08000000/02*001Mg", serial="X"\n')

    def test_only_alt_zero_and_one_device_are_accepted(self):
        option_bytes = listing().replace('alt=0', 'alt=1').replace(
            '@Internal Flash /0x08000000/', '@Option Bytes /0x1fffc000/')
        self.assertEqual(self.discover(option_bytes + listing()), 'ABC123')
        with self.assertRaises(rom_flash.RomFlashError):
            self.discover(listing('A') + listing('B'))
        self.assertIsNone(self.discover(listing('A'), expected='B'))

    def test_driver_failure_is_reported_before_selection(self):
        with patch.object(rom_flash.sys, 'platform', 'win32'), \
                self.assertRaises(rom_flash.RomFlashError) as caught:
            self.discover('Cannot open DFU device 0483:df11')
        self.assertIn('WinUSB', str(caught.exception))

    def test_macos_device_access_failure_does_not_request_a_windows_driver(self):
        with patch.object(rom_flash.sys, 'platform', 'darwin'), \
                self.assertRaises(rom_flash.RomFlashError) as caught:
            self.discover('Cannot open DFU device 0483:df11')
        self.assertIn('Close other software', str(caught.exception))
        self.assertNotIn('WinUSB', str(caught.exception))


class Backup(unittest.TestCase):
    def test_metadata_authenticates_the_private_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            backup = pathlib.Path(directory) / 'backup.bin'
            backup.write_bytes(b'private radio data')
            metadata_path = rom_flash.write_backup_metadata(backup, 'NB4+', 'A/B:C')
            metadata = json.loads(metadata_path.read_text(encoding='utf-8'))
            self.assertEqual(metadata['radio_variant'], 'NB4+')
            self.assertEqual(metadata['size'], backup.stat().st_size)
            self.assertEqual(len(metadata['sha256']), 64)
            self.assertEqual(rom_flash.safe_serial('A/B:C'), 'A_B_C')

    def test_backup_progress_cannot_look_like_a_completed_install(self):
        values = []

        def stream(args, progress, **_kwargs):
            pathlib.Path(args[-1]).write_bytes(b'\xff' * rom_flash.FLASH_SIZE)
            progress('Backing up 100%', 21)
            return 0

        with tempfile.TemporaryDirectory() as directory, \
                patch.object(rom_flash, 'dfu_util_path', return_value='dfu-util'), \
                patch.object(rom_flash, 'discover_rom_dfu', return_value='SERIAL'), \
                patch.object(rom_flash, '_stream_dfu', side_effect=stream):
            rom_flash.backup_internal(pathlib.Path(directory) / 'backup.bin',
                                      lambda _message, percent: values.append(percent))
        self.assertEqual(values, [1, 21, 22])
        self.assertNotIn(100, values)


class Progress(unittest.TestCase):
    def test_erase_and_write_have_separate_monotonic_progress_ranges(self):
        output = ('Erase [ ] 0%\rErase [==] 50%\rErase [====] 100%\n'
                  'Download [ ] 0%\rDownload [==] 50%\rDownload [====] 100%\n')
        process = SimpleNamespace(stdout=io.StringIO(output), returncode=0, wait=lambda: None)
        reports = []
        with patch.object(rom_flash.subprocess, 'Popen', return_value=process):
            result = rom_flash._stream_dfu(['dfu-util', '-D', 'image.bin'],
                lambda message, percent: reports.append((message, percent)),
                lo=25, hi=69, phase='Writing')
        self.assertEqual(result, 0)
        self.assertEqual([percent for _, percent in reports], [25, 36, 47, 58, 69])
        self.assertEqual(reports[0][0], 'Erasing flash 0%')
        self.assertEqual(reports[-1][0], 'Writing 100%')


class Installation(unittest.TestCase):
    def test_verified_new_image_uses_the_resume_entry_and_reports_automatic_start(self):
        image = full_image(auto_start=True)
        operations, reports = [], []
        def stream(args, _progress, **_kwargs):
            if '-D' in args:
                self.assertEqual(pathlib.Path(args[-1]).read_bytes(), image)
                operations.append('write')
            else:
                pathlib.Path(args[-1]).write_bytes(image)
                operations.append('readback')
            return 0
        def leave(args, **_kwargs):
            self.assertEqual(operations, ['write', 'readback'])
            self.assertEqual(args[-1], '0x8000048:leave')
            operations.append('leave')
        with patch.object(rom_flash, 'dfu_util_path', return_value='dfu-util'), \
                patch.object(rom_flash, 'discover_rom_dfu', side_effect=['SERIAL', 'SERIAL', None]), \
                patch.object(rom_flash, '_stream_dfu', side_effect=stream), \
                patch.object(rom_flash, '_run', side_effect=leave), \
                patch.object(rom_flash.time, 'sleep', lambda _: None):
            rom_flash.run_install(image, lambda message, percent: reports.append((message, percent)))
        self.assertEqual(operations, ['write', 'readback', 'leave'])
        self.assertEqual(reports[-1][1], 100)
        self.assertIn('starting automatically', reports[-1][0])

    def test_readback_mismatch_never_sends_leave(self):
        calls = []

        def stream(args, _progress, **_kwargs):
            if '-U' in args:
                pathlib.Path(args[-1]).write_bytes(b'\x00' * rom_flash.FLASH_SIZE)
            return 0

        with patch.object(rom_flash, 'dfu_util_path', return_value='dfu-util'), \
                patch.object(rom_flash, 'discover_rom_dfu', return_value='SERIAL'), \
                patch.object(rom_flash, '_stream_dfu', side_effect=stream), \
                patch.object(rom_flash, '_run', side_effect=lambda args, **_kw: calls.append(args)):
            with self.assertRaises(rom_flash.RomFlashError) as caught:
                rom_flash.run_install(full_image(auto_start=True), lambda *_: None)
        self.assertIn('does not match', str(caught.exception))
        self.assertEqual(calls, [])


if __name__ == '__main__':
    unittest.main()
