# SPDX-License-Identifier: GPL-2.0-only
import copy
import json
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import nb4_maintenance as maintenance


def inventory():
    return dict(format='apextx-inventory-1', serial='000000000001', target='nb4',
                epoch=3, car_key=4, connected=True, two_way=True,
                module=dict(valid=True, unavailable=False, product=1, hardware=2,
                            firmware=3, rf_library=4, firmware_known=True),
                receiver=dict(valid=True, unavailable=False, product=4,
                              identity_words=[7, 2, 3, 4, 5], firmware=4))


class Port:
    def __init__(self, **kwargs):
        self.port = None
        self.dtr = self.rts = True
        self.pending = b''
        self.closed = False

    def open(self):
        assert not self.dtr and not self.rts

    def write(self, frame):
        command, request, _ = maintenance.Decoder().feed(frame)[0]
        value = (dict(protocol=1, inventory=True, rf_write=False)
                 if command == 1 else inventory())
        self.pending += maintenance.encode(command | 0x80, request, json.dumps(value).encode())
        return len(frame)

    def read(self, size):
        # Real serial reads may fragment anywhere, including inside the CRC.
        result, self.pending = self.pending[:7], self.pending[7:]
        return result

    def close(self):
        self.closed = True


class MaintenanceTests(unittest.TestCase):
    def test_fragmented_adjacent_damaged_and_oversized_packets(self):
        decoder = maintenance.Decoder()
        packet = maintenance.encode(2, 17, b'{"valid":true}')
        self.assertEqual(decoder.feed(packet[:-1]), [])
        self.assertEqual(decoder.feed(packet[-1:] + packet),
                         [(2, 17, b'{"valid":true}')] * 2)
        self.assertEqual(decoder.feed(packet[:-1] + bytes([packet[-1] ^ 1])), [])
        invalid = maintenance.HEADER.pack(b'APXM', 1, 2, 65535, 0)
        self.assertEqual(decoder.feed(invalid + packet), [(2, 17, b'{"valid":true}')])
        with self.assertRaises(maintenance.MaintenanceError):
            maintenance.encode(2, 1, b'x' * 1025)

    def test_reads_one_matching_device_and_never_toggles_reset_lines(self):
        port = Port()
        info = SimpleNamespace(serial_number='000000000001', vid=0x0483,
                               pid=0x5740, device='test-owned-port', product='ApexTX NB4 Maintenance')
        result = maintenance.read_inventory('000000000001', 'nb4',
                                            ports=[info], factory=lambda **_: port)
        self.assertEqual(result, inventory())
        self.assertTrue(port.closed)
        for devices in ([], [info, info]):
            with patch.object(port, 'open', side_effect=AssertionError('must not open')):
                with self.assertRaises(maintenance.MaintenanceError):
                    maintenance.read_inventory('000000000001', 'nb4', ports=devices,
                                               factory=lambda **_: port)

    def test_client_rejects_write_commands_and_old_sequence(self):
        stream = Port()
        with self.assertRaisesRegex(maintenance.MaintenanceError, 'only reads'):
            maintenance.Client(stream).request(18)
        original = stream.write

        def delayed(frame):
            command, request, _ = maintenance.Decoder().feed(frame)[0]
            stream.pending += maintenance.encode(command | 0x80, request ^ 1, b'{"old":true}')
            return original(frame)

        stream.write = delayed
        self.assertEqual(maintenance.Client(stream).request(1)['protocol'], 1)

    def test_identity_and_state_are_not_coerced(self):
        original = inventory()
        self.assertEqual(maintenance.validate_inventory(original, original['serial'], 'nb4'), original)
        for path, value in (('serial', 'other'), ('target', 'nb4plus'), ('epoch', True),
                            ('connected', False), ('two_way', False)):
            candidate = copy.deepcopy(original)
            candidate[path] = value
            with self.subTest(path=path), self.assertRaises(maintenance.MaintenanceError):
                maintenance.validate_inventory(candidate, original['serial'], 'nb4')
        candidate = copy.deepcopy(original)
        candidate['receiver']['firmware'] = True
        with self.assertRaises(maintenance.MaintenanceError):
            maintenance.validate_inventory(candidate, original['serial'], 'nb4')

    def test_timeout_duplicate_keys_and_device_rejection(self):
        for response in (b'{"protocol":1,"protocol":2}', b'{"error":"unsupported_operation"}'):
            port = Port()
            def answer(frame):
                cmd, req, _ = maintenance.Decoder().feed(frame)[0]
                port.pending = maintenance.encode(cmd | 0x80, req, response)
                return len(frame)
            port.write = answer
            with self.assertRaises(maintenance.MaintenanceError):
                maintenance.Client(port).request(1)
        port = Port()
        port.write = lambda frame: len(frame)
        clock = iter(range(20))
        with self.assertRaisesRegex(maintenance.MaintenanceError, 'No maintenance response'):
            maintenance.Client(port, clock=lambda: next(clock)).request(1)

    def test_session_checks_device_before_and_after_read_and_expires_snapshot(self):
        from nb4_flasher_session import FlasherSession
        from unittest.mock import Mock
        device = dict(state='normal', serial='000000000001', target=1, maintenance=True)
        session = FlasherSession(client=Mock())
        session.device = device
        session.begin('inventory')
        with patch('nb4_flasher_session.probe', return_value=device), \
                patch.object(maintenance, 'read_inventory', return_value=inventory()):
            session.read_inventory(device)
        self.assertFalse(session.status['running'])
        self.assertFalse(session.status['error'])
        self.assertTrue(session.snapshot()['components_fresh'])
        session.inventory_time -= 11
        self.assertFalse(session.snapshot()['components_fresh'])
        session.client.latest.return_value = None
        with patch('nb4_flasher_session.probe', return_value=dict(device, maintenance=False)):
            session.scan()
        self.assertIsNone(session.snapshot()['components'])
        for probes in ([dict(state='absent')], [device, dict(device, serial='another')]):
            session.begin('inventory')
            with patch('nb4_flasher_session.probe', side_effect=probes), \
                    patch.object(maintenance, 'read_inventory', return_value=inventory()):
                session.read_inventory(device)
            self.assertTrue(session.status['error'])
            self.assertIsNone(session.snapshot()['components'])

    def test_regular_serial_port_is_never_probed_with_maintenance_bytes(self):
        info = SimpleNamespace(serial_number='000000000001', vid=0x0483,
                               pid=0x5740, device='ordinary-debug', product='ApexTX NB4 Serial Port')
        with patch.object(Port, 'open', side_effect=AssertionError('must not open')):
            with self.assertRaisesRegex(maintenance.MaintenanceError, 'ordinary serial'):
                maintenance.read_inventory('000000000001', 'nb4', ports=[info], factory=Port)


if __name__ == '__main__':
    unittest.main()
