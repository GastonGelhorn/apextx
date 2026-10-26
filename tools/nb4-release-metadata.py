#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Publish bootloader requirements from the exact validated release image."""
import argparse
import hashlib
import json
import re
from pathlib import Path

from nb4_releases import Version, firmware_identity, image_bootloader_generation
from nb4_update_image import BOOT_SIZE, TARGETS, package_target, update_package, validate_full_image_auth


def metadata(image, version, model):
    Version(version)
    validate_full_image_auth(image)
    target = TARGETS[model]
    if package_target(update_package(image)) != target:
        raise ValueError('The release image is for another radio.')
    stamps = re.findall(rb'apextx-nb4-[^\x00]{1,110}\x00', image[BOOT_SIZE:BOOT_SIZE + 1024])
    if not any(firmware_identity(s[:-1].decode('ascii', errors='replace'))['version'] == version
               and not firmware_identity(s[:-1].decode('ascii', errors='replace'))['development'] for s in stamps):
        raise ValueError('The release image version does not match.')
    generation = image_bootloader_generation(image)
    if not generation:
        raise ValueError('The release image must identify its bootloader generation.')
    return dict(schema=1, version=version, target=target, bootloader=generation,
                firmware=f'apextx-{model}-{version}.bin', sha256=hashlib.sha256(image).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--model', choices=TARGETS, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(metadata(args.image.read_bytes(), args.version, args.model), indent=2) + '\n')


if __name__ == '__main__':
    main()
