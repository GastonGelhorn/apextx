#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build/validate the NB4 application manifest used by the ApexTX bootloader."""
import argparse
import re
import struct
import zlib
from pathlib import Path

FLASH_SIZE = 0x200000
BOOT_SIZE = 0x20000
APP_ADDRESS = 0x08020000
HEADER_SIZE = 32
MAX_APP = FLASH_SIZE - BOOT_SIZE - HEADER_SIZE
HEADER = struct.Struct('<8s6I')


# Every ApexTX build carries its target as "apextx-<flavour>-<version>". The
# original NB4 is "nb4"; its siblings are "nb4p", "pl18" and so on, and their
# pin setup would drive this radio's power, display and RF incorrectly.
FLAVOUR = re.compile(rb'apextx-(nb4[a-z0-9]*)-[0-9]')


def validate_vectors(app):
    if len(app) < 8:
        raise ValueError('The firmware image is incomplete.')
    stack, reset = struct.unpack_from('<II', app)
    if (stack & 7 or not (0x10000000 < stack <= 0x1000fff0 or
                         0x20000000 < stack <= 0x20030000) or
            not reset & 1 or not APP_ADDRESS <= reset < APP_ADDRESS + len(app)):
        raise ValueError('The image contains invalid NB4 application vectors.')
    # Require the original NB4 marker and no other, so an image for a sibling
    # radio is refused even when an incidental match appears elsewhere in it.
    found = {match.group(1).decode() for match in FLAVOUR.finditer(app)}
    if found != {'nb4'}:
        listed = ', '.join(sorted(found)) or 'none'
        raise ValueError('The image is not identified as ApexTX firmware for the '
                         f'original Noble NB4 (target markers found: {listed}).')


def manifest_for(app):
    validate_vectors(app)
    if len(app) > MAX_APP or len(app) % 4:
        raise ValueError('The NB4 application size is invalid.')
    header = struct.pack('<8s5I', b'APXNB4U1', 1, APP_ADDRESS, len(app),
                         zlib.crc32(app), 1)
    return header + struct.pack('<I', zlib.crc32(header))


def update_package(image):
    """Accept a complete, manifested image; never silently bless an old binary."""
    if len(image) != FLASH_SIZE:
        raise ValueError('Select a 2 MiB firmware.bin image built for ApexTX Update.')
    header = image[-HEADER_SIZE:]
    magic, target, address, size, crc, version, header_crc = HEADER.unpack(header)
    if (magic != b'APXNB4U1' or target != 1 or address != APP_ADDRESS or version != 1 or
            not 8 <= size <= MAX_APP or size % 4 or zlib.crc32(header[:28]) != header_crc):
        raise ValueError('The NB4 firmware manifest is invalid or corrupt.')
    app = image[BOOT_SIZE:BOOT_SIZE + size]
    if zlib.crc32(app) != crc:
        raise ValueError('The firmware checksum does not match. Select an intact firmware image.')
    validate_vectors(app)
    return header + app


def finalize(image):
    if len(image) == FLASH_SIZE:
        update_package(image)
        return image
    if not BOOT_SIZE + 8 <= len(image) <= FLASH_SIZE - HEADER_SIZE:
        raise ValueError('The image must contain both the NB4 bootloader and application.')
    app = image[BOOT_SIZE:]
    app += b'\xff' * (-len(app) % 4)
    header = manifest_for(app)
    return image[:BOOT_SIZE] + app + b'\xff' * (MAX_APP - len(app)) + header


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    args = parser.parse_args()
    result = finalize(args.image.read_bytes())
    # Write through a temporary file: an interrupted post-build step must not
    # leave a half-written firmware.bin behind.
    temporary = args.image.with_suffix(args.image.suffix + '.tmp')
    temporary.write_bytes(result)
    temporary.replace(args.image)
    print(f'ApexTX NB4 update manifest: {len(result)} bytes, CRC verified')
