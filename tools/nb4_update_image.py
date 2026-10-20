#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build/validate the NB4 application manifest used by the ApexTX bootloader."""
import argparse
import hashlib
import re
import struct
import zlib
from pathlib import Path

FLASH_SIZE = 0x200000
BOOT_SIZE = 0x20000
APP_ADDRESS = 0x08020000
HEADER_SIZE = 32
HEADER = struct.Struct('<8s6I')
FULL_IMAGE_AUTH = struct.Struct('<8s32s')
FULL_IMAGE_AUTH_OFFSET = FLASH_SIZE - HEADER_SIZE - FULL_IMAGE_AUTH.size
MAX_APP = FLASH_SIZE - BOOT_SIZE - HEADER_SIZE
FULL_IMAGE_MAX_APP = FULL_IMAGE_AUTH_OFFSET - BOOT_SIZE


# Every ApexTX build carries its target as "apextx-<flavour>-<version>". The
# original NB4 is "nb4"; its siblings are "nb4p", "pl18" and so on, and their
# pin setup would drive this radio's power, display and RF incorrectly.
FLAVOUR = re.compile(rb'apextx-(nb4[a-z0-9]*)-[0-9]')

# The hardware variant inside the NB4 build, as the firmware states it in its
# own image ("APXHW:<id>", from radio/src/bootloader/nb4_update_id.h),
# and the manifest target the matching bootloader accepts. The manifest target
# is always taken from the image, never passed in, so a manifest can only
# describe the binary it is attached to.
HARDWARE = re.compile(rb'APXHW:([a-z0-9]+)\x00')
TARGETS = {'nb4': 1, 'nb4plus': 2}
VARIANTS = {1: 'NB4', 2: 'NB4+'}


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


def image_target(app, legacy=False):
    """The manifest target for the hardware this application says it is for.

    Images from before the marker existed are original NB4 images; they are
    accepted only when `legacy` is set, which validating an existing release
    needs and building a new one must not.
    """
    found = {match.group(1).decode() for match in HARDWARE.finditer(app)}
    if not found and legacy:
        return TARGETS['nb4']
    if len(found) != 1 or not found <= TARGETS.keys():
        listed = ', '.join(sorted(found)) or 'none'
        raise ValueError('The image does not identify the radio it was built for '
                         f'(hardware markers found: {listed}).')
    return TARGETS[found.pop()]


def package_target(package):
    """The manifest target of a package returned by update_package."""
    return HEADER.unpack(package[:HEADER_SIZE])[1]


def _manifest(app, target):
    header = struct.pack('<8s5I', b'APXNB4U1', target, APP_ADDRESS, len(app),
                         zlib.crc32(app), 1)
    return header + struct.pack('<I', zlib.crc32(header))


def manifest_for(app):
    validate_vectors(app)
    if len(app) > FULL_IMAGE_MAX_APP or len(app) % 4:
        raise ValueError('The NB4 application size is invalid.')
    return _manifest(app, image_target(app))


def update_package(image):
    """Accept a complete, manifested image; never silently bless an old binary."""
    if len(image) != FLASH_SIZE:
        raise ValueError('Select a 2 MiB firmware.bin image built for ApexTX Update.')
    header = image[-HEADER_SIZE:]
    magic, target, address, size, crc, version, header_crc = HEADER.unpack(header)
    if (magic != b'APXNB4U1' or target not in VARIANTS or address != APP_ADDRESS or
            version != 1 or not 8 <= size <= MAX_APP or size % 4 or
            zlib.crc32(header[:28]) != header_crc):
        raise ValueError('The NB4 firmware manifest is invalid or corrupt.')
    app = image[BOOT_SIZE:BOOT_SIZE + size]
    if zlib.crc32(app) != crc:
        raise ValueError('The firmware checksum does not match. Select an intact firmware image.')
    validate_vectors(app)
    # The manifest decides which bootloader will install the image, so it has
    # to agree with what the firmware inside says it is.
    if image_target(app, legacy=True) != target:
        raise ValueError('The firmware manifest and the firmware disagree about which radio '
                         'it is for. Rebuild the image.')
    return header + app


def _full_image_digest(image):
    """Hash every byte except the digest record itself."""
    return hashlib.sha256(image[:FULL_IMAGE_AUTH_OFFSET] +
                          image[FULL_IMAGE_AUTH_OFFSET + FULL_IMAGE_AUTH.size:]).digest()


def authenticate_full_image(image):
    """Add the host-side checksum used before replacing the bootloader."""
    if len(image) != FLASH_SIZE:
        raise ValueError('Only an exact 2 MiB image can be authenticated.')
    result = bytearray(image)
    result[FULL_IMAGE_AUTH_OFFSET:FULL_IMAGE_AUTH_OFFSET + FULL_IMAGE_AUTH.size] = \
        FULL_IMAGE_AUTH.pack(b'APXFULL1', _full_image_digest(result))
    return bytes(result)


def validate_full_image_auth(image):
    """Verify the bootloader, application padding and manifest as one image."""
    if len(image) != FLASH_SIZE:
        raise ValueError('Select an exact 2 MiB ApexTX firmware.bin.')
    magic, expected = FULL_IMAGE_AUTH.unpack_from(image, FULL_IMAGE_AUTH_OFFSET)
    if magic != b'APXFULL1' or expected != _full_image_digest(image):
        raise ValueError('The complete-image checksum is missing or does not match. '
                         'Select an intact current ApexTX firmware.bin.')


def finalize(image):
    if len(image) == FLASH_SIZE:
        package = update_package(image)
        auth = image[FULL_IMAGE_AUTH_OFFSET:FULL_IMAGE_AUTH_OFFSET + FULL_IMAGE_AUTH.size]
        if auth.startswith(b'APXFULL1'):
            validate_full_image_auth(image)
            return image
        if auth != b'\xff' * FULL_IMAGE_AUTH.size:
            raise ValueError('The complete-image checksum area is not empty or valid.')
        if len(package) - HEADER_SIZE > FULL_IMAGE_MAX_APP:
            raise ValueError('The application leaves no room for the complete-image checksum.')
        return authenticate_full_image(image)
    if not BOOT_SIZE + 8 <= len(image) <= FLASH_SIZE - HEADER_SIZE:
        raise ValueError('The image must contain both the NB4 bootloader and application.')
    app = image[BOOT_SIZE:]
    app += b'\xff' * (-len(app) % 4)
    header = manifest_for(app)
    result = image[:BOOT_SIZE] + app + b'\xff' * (FULL_IMAGE_MAX_APP - len(app))
    result += b'\xff' * FULL_IMAGE_AUTH.size + header
    return authenticate_full_image(result)


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
    validate_full_image_auth(result)
    variant = VARIANTS[package_target(update_package(result))]
    print(f'ApexTX {variant} update manifest: {len(result)} bytes, '
          'CRC and complete-image SHA-256 verified')
