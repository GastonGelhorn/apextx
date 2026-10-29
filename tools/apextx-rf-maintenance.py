#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Explicit RF bench preparation, installation, status and recovery."""
import argparse
import json
from pathlib import Path
import struct
from nb4_maintenance import connection, TARGETS, STATUS
from nb4_usb_update import probe
from nb4_rf_catalog import verify_bundle, MAX_BUNDLE, IMAGES, image_name
from nb4_rf_transfer import RfTransfer, plan, validate_status


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--stage', type=Path, metavar='RF_CATALOG.zip')
    mode.add_argument('--install', action='store_true')
    mode.add_argument('--recover', action='store_true')
    mode.add_argument('--status', action='store_true')
    mode.add_argument('--cancel', action='store_true')
    parser.add_argument('--image', choices=('module','fgr4-v2','fgr4s-v2'))
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--operation', required=True, type=Path, metavar='PRIVATE_DIRECTORY')
    parser.add_argument('--serial', required=True)
    parser.add_argument('--confirm-unit', metavar='SERIAL')
    parser.add_argument('--reinstall', action='store_true')
    args = parser.parse_args()
    if args.stage and not (args.image and args.backup): parser.error('Staging requires --image and --backup.')
    if not args.stage and (args.image or args.backup): parser.error('--image/--backup are staging options.')
    if args.reinstall and not args.install: parser.error('--reinstall requires --install.')
    if (args.install or args.recover) and args.confirm_unit != args.serial:
        parser.error('Review the plan, then confirm the exact serial with --confirm-unit.')
    try:
        device = probe()
        if (device.get('state') != 'normal' or device.get('maintenance') is not True or
                device.get('serial') != args.serial or device.get('target') not in TARGETS):
            raise RuntimeError('Connect only the selected radio from System > Update > Connect to ApexTX Flasher.')
        with connection(args.serial, TARGETS[device['target']], verified=True) as port:
            transfer = RfTransfer(port, args.serial, TARGETS[device['target']], args.operation)
            progress = lambda message, percent: print(f'{percent:3d}% {message}', flush=True)
            if args.stage:
                with args.stage.open('rb') as stream: catalog, images = verify_bundle(stream.read(MAX_BUNDLE + 1))
                if catalog['radio'] != TARGETS[device['target']]:
                    raise RuntimeError('The RF catalog targets a different radio.')
                index = ('module','fgr4-v2','fgr4s-v2').index(args.image)
                result = transfer.stage(index, images[image_name(IMAGES[index])], args.backup, progress=progress)
            else:
                record = transfer.load()
                if args.install:
                    result = transfer.start(confirmed=True, reinstall=args.reinstall, progress=progress)
                elif args.recover:
                    result = transfer.recover(confirmed=True, progress=progress)
                elif args.cancel:
                    result = transfer.cancel()
                else:
                    result = transfer.reconcile()
            print(json.dumps(result, indent=2))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f'Error: {error}\n')
