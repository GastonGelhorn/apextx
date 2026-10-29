# SPDX-License-Identifier: GPL-2.0-only
"""Bounded, explicit USB-CDC inventory requests. This client cannot write RF."""
import json
import secrets
import struct
import time
import zlib
from contextlib import contextmanager

MAGIC = b'APXM'
VERSION = 1
MAX_PAYLOAD = 1024
HEADER = struct.Struct('<4sBBHI')
CAPABILITIES, INVENTORY = 1, 2
STORAGE_SESSION = 3
PREPARE, UPLOAD, START, STATUS, CANCEL = 16, 17, 18, 19, 20
RECOVER = 21
# Catalog assets stored on the card for the radio's own installer, and the
# restart into ApexTX Update. Neither command writes an RF component.
ASSET_BEGIN, ASSET_DATA, REBOOT_UPDATE = 22, 23, 24
TARGETS = {1: 'nb4', 2: 'nb4plus'}


class MaintenanceError(RuntimeError):
    pass


def encode(command, request, payload=b''):
    if (type(command) is not int or not 0 <= command <= 255 or
            type(request) is not int or not 0 <= request <= 0xffffffff or
            not isinstance(payload, bytes) or len(payload) > MAX_PAYLOAD):
        raise MaintenanceError('Invalid maintenance request.')
    frame = HEADER.pack(MAGIC, VERSION, command, len(payload), request) + payload
    return frame + struct.pack('<I', zlib.crc32(frame))


class Decoder:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        if len(data) > 4096:
            raise MaintenanceError('Maintenance input exceeded its limit.')
        self.buffer.extend(data)
        result = []
        while True:
            start = self.buffer.find(MAGIC)
            if start < 0:
                self.buffer = self.buffer[-3:]
                break
            del self.buffer[:start]
            if len(self.buffer) < HEADER.size:
                break
            _, version, command, length, request = HEADER.unpack_from(self.buffer)
            if version != VERSION or length > MAX_PAYLOAD:
                del self.buffer[:4]
                continue
            size = HEADER.size + length + 4
            if len(self.buffer) < size:
                break
            frame = bytes(self.buffer[:size])
            del self.buffer[:size]
            if zlib.crc32(frame[:-4]) == struct.unpack('<I', frame[-4:])[0]:
                result.append((command, request, frame[HEADER.size:-4]))
        return result


def unique_object(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            raise MaintenanceError('Duplicate maintenance response key.')
        result[name] = value
    return result


def validate_inventory(value, serial, target):
    if (not isinstance(value, dict) or value.get('format') != 'apextx-inventory-1' or
            value.get('serial') != serial or value.get('target') != target):
        raise MaintenanceError('The maintenance device identity changed. Nothing was written.')
    for key in ('epoch', 'car_key'):
        if type(value.get(key)) is not int or not 0 <= value[key] <= 0xffffffff:
            raise MaintenanceError('Invalid inventory session.')
    for key in ('connected', 'two_way'):
        if type(value.get(key)) is not bool:
            raise MaintenanceError('Invalid inventory connection state.')
    for key, fields in (
        ('module', ('product', 'hardware', 'firmware', 'rf_library')),
        ('receiver', ('product', 'firmware')),
    ):
        entry = value.get(key)
        if not isinstance(entry, dict):
            raise MaintenanceError('Incomplete component inventory.')
        if any(type(entry.get(flag)) is not bool for flag in ('valid', 'unavailable')):
            raise MaintenanceError('Invalid component validity.')
        for field in fields:
            maximum = 0xffffffff if key == 'module' or field == 'product' else 0xffff
            if type(entry.get(field)) is not int or not 0 <= entry[field] <= maximum:
                raise MaintenanceError('Invalid component version.')
    if type(value['module'].get('firmware_known')) is not bool:
        raise MaintenanceError('Invalid module version layout.')
    words = value['receiver'].get('identity_words')
    if (not isinstance(words, list) or len(words) != 5 or
            any(type(word) is not int or not 0 <= word <= 0xffff for word in words) or
            value['receiver']['firmware'] != words[3]):
        raise MaintenanceError('Invalid receiver identity words.')
    if value['receiver']['valid'] and not (value['connected'] and value['two_way']):
        raise MaintenanceError('A disconnected receiver cannot supply a valid inventory.')
    if 'application' in value:
        app=value['application']
        if (not isinstance(app,dict) or type(app.get('valid')) is not bool or
                any(type(app.get(key)) is not int or not 0 <= app[key] <= maximum
                    for key,maximum in (('bytes',0x1dffe0),('crc32',0xffffffff)))):
            raise MaintenanceError('Invalid transmitter application identity.')
    audio=value.get('audio',{'state':'unknown'})
    if not isinstance(audio,dict) or audio.get('state') not in ('unknown','installed','pending'):
        raise MaintenanceError('Invalid installed audio identity.')
    if audio.get('state')=='installed':
        import re
        if (audio.get('language') not in ('en','es','de','cn') or
                not isinstance(audio.get('release'),str) or not re.fullmatch('[A-Za-z0-9.+_-]{1,31}',audio['release'])):
            raise MaintenanceError('Invalid installed audio version.')
    return value


class Client:
    allowed_commands = (CAPABILITIES, INVENTORY)
    def __init__(self, stream, *, clock=time.monotonic):
        self.stream, self.clock = stream, clock
        self.decoder = Decoder()

    def request(self, command, payload=b''):
        if command not in self.allowed_commands:
            raise MaintenanceError('This client only reads inventory and capabilities.')
        request = secrets.randbits(32)
        frame = encode(command, request, payload)
        if self.stream.write(frame) != len(frame):
            raise MaintenanceError('The maintenance request was not fully transferred.')
        end, total = self.clock() + 5, 0
        while self.clock() < end:
            data = self.stream.read(1024)
            total += len(data)
            if total > 16384:
                raise MaintenanceError('The maintenance device sent too much data.')
            for opcode, identifier, payload in self.decoder.feed(data):
                if (opcode, identifier) != (command | 0x80, request):
                    continue
                try:
                    value = json.loads(payload, object_pairs_hook=unique_object)
                except (UnicodeError, ValueError) as error:
                    raise MaintenanceError('Invalid maintenance response.') from error
                if not isinstance(value, dict) or 'error' in value:
                    reason = value.get('error', 'invalid_response') if isinstance(value, dict) else 'invalid_response'
                    raise MaintenanceError(f'The radio rejected the maintenance request: {reason}.')
                return value
        raise MaintenanceError('No maintenance response. On the radio open System > Update > Connect to ApexTX Flasher, then reconnect USB.')


@contextmanager
def connection(serial, target, *, ports=None, factory=None, verified=False):
    if not isinstance(serial, str) or not serial or target not in TARGETS.values():
        raise MaintenanceError('Select one identified ApexTX radio first.')
    if ports is None or factory is None:
        try:
            import serial as pyserial
            from serial.tools import list_ports
        except ImportError as error:
            raise MaintenanceError('The flasher serial runtime is missing; install requirements-updater.txt.') from error
        if ports is None:
            ports = list_ports.comports()
        if factory is None:
            factory = pyserial.Serial
    candidates = [port for port in ports
                  if port.serial_number == serial and (port.vid, port.pid) == (0x0483, 0x5740)]
    if len(candidates) != 1:
        raise MaintenanceError('Reconnect the selected radio using Connect to ApexTX Flasher. A unique USB serial port is required.')
    model = 'NB4' if target == 'nb4' else 'NB4+'
    if not verified and getattr(candidates[0], 'product', None) != f'ApexTX {model} Maintenance':
        raise MaintenanceError('This is an ordinary serial/debug port. Reconnect using Connect to ApexTX Flasher.')
    stream = factory(port=None, baudrate=115200, timeout=0.15, write_timeout=2)
    try:
        # Set lines before opening; never guess reset/bootloader-entry pulses.
        stream.dtr = stream.rts = False
        stream.port = candidates[0].device
        stream.open()
        yield stream
    except OSError as error:
        raise MaintenanceError('Could not read the selected maintenance port. Close other serial tools and reconnect.') from error
    finally:
        stream.close()


def read_inventory(serial, target, **options):
    with connection(serial, target, **options) as stream:
        client = Client(stream)
        capabilities = client.request(CAPABILITIES)
        if type(capabilities.get('protocol')) is not int or capabilities['protocol'] != VERSION or capabilities.get('inventory') is not True:
            raise MaintenanceError('This radio does not support the inventory protocol.')
        return validate_inventory(client.request(INVENTORY), serial, target)
