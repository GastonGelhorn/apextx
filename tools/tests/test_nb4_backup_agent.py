# SPDX-License-Identifier: GPL-2.0-only
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import nb4_backup_agent as agent
from test_nb4_rom_flash import full_image


class SyntheticAgent:
    def __init__(self, corrupt=False):
        self.internal = full_image()
        self.nor = bytes(range(256)) * (8388608 // 256)
        self.corrupt, self.reads = corrupt, 0

    def read(self, address, size):
        if address == 0x91000000:
            return agent.INFO.pack(b'APXBKP3\0', 3, 0x10016419, 1, 0x12340000,
                                   2, 0xef4017, len(self.nor), len(self.internal),0,0)
        if address >= 0x90000000:
            offset = address - 0x90000000
            self.reads += 1
            data = self.nor[offset:offset+size]
            return b'x' + data[1:] if self.corrupt and self.reads == 129 else data
        offset = address - 0x08000000
        return self.internal[offset:offset+size]


class BackupAgentTests(unittest.TestCase):
    def test_inlined_timer_must_increment_the_hal_counter(self):
        spec = importlib.util.spec_from_file_location('backup_agent_verifier',
            Path(__file__).resolve().parents[1]/'nb4-verify-backup-agent.py')
        verifier = importlib.util.module_from_spec(spec); spec.loader.exec_module(verifier)
        prefix = bytes.fromhex('024a136801331360704700bf')
        symbols = {'SysTick_Handler': agent.RAM, 'uwTick': agent.RAM + 1024}
        verifier.verify_hal_time_base(prefix + struct.pack('<I', symbols['uwTick']), symbols, '')
        # A separate timer can tick while HAL's USB initialization waits forever.
        for bad in (prefix + struct.pack('<I', symbols['uwTick'] + 4), prefix, b'\x70\x47'):
            with self.subTest(image=bad), self.assertRaises(ValueError):
                verifier.verify_hal_time_base(bad, symbols, '')

    def test_linked_crt_must_include_its_return_not_only_the_referenced_entry(self):
        spec = importlib.util.spec_from_file_location('backup_agent_verifier',
            Path(__file__).resolve().parents[1]/'nb4-verify-backup-agent.py')
        verifier = importlib.util.module_from_spec(spec); spec.loader.exec_module(verifier)
        complete = bytes.fromhex('f8b500bff8bc08bc9e467047')
        symbols = {'_init': agent.RAM, '_fini': agent.RAM + len(complete)}
        verifier.verify_crt_returns(complete * 2, symbols)
        # The failed link retained each prologue but discarded its crtn.o
        # epilogue, so execution fell through into the next section.
        for damaged in (b'\xf8\xb5\x00\xbf' * 2, complete + b'\xf8\xb5\x00\xbf', b''):
            with self.subTest(image=damaged), self.assertRaises(ValueError):
                verifier.verify_crt_returns(damaged, symbols)
        with self.assertRaises(ValueError): verifier.verify_crt_returns(complete * 2, {})

    def test_hardware_is_identified_by_validated_image_not_markers_or_pid(self):
        self.assertEqual(agent.identify_flash(full_image()), 'NB4')
        self.assertEqual(agent.identify_flash(full_image(b'nb4plus')), 'NB4+')
        corrupt = bytearray(full_image()); corrupt[0x20010] ^= 1
        with self.assertRaises(agent.RomFlashError):
            agent.identify_flash(bytes(corrupt))
        for invalid in (b'', b'\0' * 2097152):
            with self.assertRaises(agent.RomFlashError): agent.identify_flash(invalid)

    def test_only_exact_checksummed_sram_images_are_executed(self):
        data = bytearray(1024)
        struct.pack_into('<II', data, 0, agent.RAM + agent.RAM_CAPACITY, 0x20010101)
        digest = hashlib.sha256(data).hexdigest()
        agent.validate_agent(bytes(data), digest)
        for stack, reset in ((0x1000fff0, 0x20010101), (agent.RAM + agent.RAM_CAPACITY, 0x08000101),
                             (agent.RAM + agent.RAM_CAPACITY, 0x20010100),
                             (agent.RAM + agent.RAM_CAPACITY, 0x20020001),
                             (0x20030000, 0x20010101)):
            bad = bytearray(data); struct.pack_into('<II', bad, 0, stack, reset)
            with self.assertRaises(agent.RomFlashError):
                agent.validate_agent(bytes(bad), hashlib.sha256(bad).hexdigest())
        with self.assertRaises(agent.RomFlashError):
            agent.validate_agent(bytes(data), '0' * 64)

    def test_launch_only_downloads_sram_and_never_leaves_without_readback(self):
        data = bytearray(1024); struct.pack_into('<II', data, 0, agent.RAM + agent.RAM_CAPACITY, 0x20010101)
        data = bytes(data); calls = []
        def run(args, **_):
            calls.append(args)
            if '-U' in args: Path(args[-1]).write_bytes(data)
            return SimpleNamespace(returncode=0)
        with patch.object(agent, 'discover_rom_dfu', return_value='000000031234'), \
                patch.object(agent, 'dfu_util_path', return_value='dfu-util'), \
                patch.object(agent, '_run', side_effect=run):
            self.assertEqual(agent.launch(data, hashlib.sha256(data).hexdigest(),
                             full_image(), '000000031234', lambda *_: None), 'NB4')
        self.assertEqual(len(calls), 3)
        self.assertTrue(all(command[command.index('-s')+1].startswith('0x20010000') for command in calls))
        self.assertEqual([command[command.index('-s')+1] for command in calls],
                         ['0x20010000:force','0x20010000:1024:force','0x20010000:force:leave'])
        calls.clear()
        with patch.object(agent, '_run', side_effect=AssertionError('must not touch USB')):
            with self.assertRaises(agent.RomFlashError):
                agent.launch(data, '0' * 64, full_image(), '000000031234', lambda *_: None)

    def test_backs_up_both_memories_and_checks_a_second_physical_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            peer = SyntheticAgent(); root = Path(directory) / 'private'
            progress = []
            result = agent.capture(peer, peer.internal, '000000031234', root,
                                   lambda message, percent: progress.append((message, percent)))
            self.assertTrue(result['raw_nor'])
            self.assertEqual(agent.verify(root), result)
            self.assertEqual(peer.reads, 256)
            self.assertEqual([value for _, value in progress], sorted(value for _, value in progress))
            self.assertEqual(progress[-1][1], 100)
            self.assertTrue(any(message == 'Verifying external storage' for message, _ in progress))
            (root / 'nor.bin').write_bytes(b'wrong')
            with self.assertRaises(agent.RomFlashError): agent.verify(root)

    def test_corrupt_second_pass_or_other_unit_cannot_create_verified_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            peer = SyntheticAgent(corrupt=True); root = Path(directory) / 'private'
            with self.assertRaises(agent.RomFlashError):
                agent.capture(peer, peer.internal, '000000031234', root)
            self.assertFalse((root / 'backup.json').exists())
            with self.assertRaises(agent.RomFlashError): agent.read_info(peer, 'another-radio')

    def test_failed_memory_identification_reports_the_actual_values(self):
        peer = SyntheticAgent()
        original = peer.read
        def invalid_memory(address, size):
            values = list(agent.INFO.unpack(original(address, size)))
            values[6], values[7] = 0xffffff, 0
            return agent.INFO.pack(*values)
        peer.read = invalid_memory
        with self.assertRaisesRegex(agent.RomFlashError, 'JEDEC=0xffffff, NOR=0, internal=2097152'):
            agent.read_info(peer, '000000031234')
        self.assertEqual(peer.reads, 0)

    def test_shared_pid_and_unknown_main_image_do_not_launch_the_agent(self):
        with patch.object(agent, '_run', side_effect=AssertionError('no write')):
            with self.assertRaises(agent.RomFlashError):
                agent.launch(b'x' * 1024, hashlib.sha256(b'x' * 1024).hexdigest(),
                             b'\0' * 2097152, '000000031234', lambda *_: None)

    def test_corrupt_identity_and_duplicate_manifest_keys_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            peer = SyntheticAgent(); root = Path(directory) / 'private'
            result = agent.capture(peer, peer.internal, '000000031234', root)
            path = root / 'backup.json'
            for key, invalid in (('serial', '000000001234'), ('uid', [True, 1, 2]),
                                 ('mcu', 0), ('nor_bytes', 4096)):
                changed = dict(result, identity=dict(result['identity'], **{key:invalid}))
                path.write_text(json.dumps(changed))
                with self.subTest(key=key), self.assertRaises(agent.RomFlashError):
                    agent.verify(root)
            path.write_text('{"format":"apextx-full-unit-backup-1","format":"different"}')
            with self.assertRaises(RuntimeError):
                agent.verify(root)

    def test_initial_mcu_backup_survives_a_failed_agent_launch(self):
        spec = importlib.util.spec_from_file_location('full_backup_cli',
            Path(__file__).resolve().parents[1]/'apextx-full-backup.py')
        cli = importlib.util.module_from_spec(spec); spec.loader.exec_module(cli)
        data=bytearray(1024);struct.pack_into('<II',data,0,agent.RAM + agent.RAM_CAPACITY,0x20010101)
        with tempfile.TemporaryDirectory() as directory:
            image=Path(directory)/'agent.bin'; image.write_bytes(data)
            output=Path(directory)/'complete'
            with patch.object(agent,'discover_rom_dfu',return_value='000000031234'), \
                    patch.object(agent,'backup_internal',side_effect=lambda path,*_:path.write_bytes(full_image())), \
                    patch.object(agent,'launch',side_effect=agent.RomFlashError('RAM transfer failed')):
                with self.assertRaises(agent.RomFlashError):
                    cli.run(image,hashlib.sha256(data).hexdigest(),output)
            saved=list(Path(directory).glob('complete-before-agent-*/internal.bin'))
            self.assertEqual(len(saved),1)
            self.assertEqual(saved[0].read_bytes(),full_image())
            self.assertTrue(saved[0].with_suffix('.bin.json').is_file())
            self.assertFalse(output.exists())

    def test_discovery_skips_an_incomplete_reconnect_and_other_units(self):
        class Device:
            iProduct=1; iSerialNumber=2
            def __init__(self, serial, unreadable=False): self.serial=serial; self.unreadable=unreadable
            def __iter__(self):
                class Config(list): bConfigurationValue=1
                return iter([Config([SimpleNamespace(bInterfaceClass=0xfe,bInterfaceSubClass=1,
                    bInterfaceProtocol=2,iInterface=4,bInterfaceNumber=0,bAlternateSetting=0)])])
        incomplete=Device('000000031234',True)
        other=Device('000000041234')
        selected=Device('000000031234')
        disposed=[]
        def string(dev,index):
            if dev.unreadable: raise ValueError('The device has no langid')
            return {1:agent.PRODUCT,2:dev.serial,4:agent.INTERFACE}[index]
        core=SimpleNamespace(find=lambda **_:[incomplete,other,selected])
        util=SimpleNamespace(get_string=string,dispose_resources=disposed.append)
        with patch.object(agent,'usb_modules',return_value=(core,util,None)):
            result=agent.discover('000000031234')
        self.assertIs(result.device,selected)
        self.assertEqual(disposed,[incomplete,other])


if __name__ == '__main__': unittest.main()
