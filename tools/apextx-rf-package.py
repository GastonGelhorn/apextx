#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Inspect/prepare local RF assets. Never connects to or writes a radio."""
import argparse
import json
from pathlib import Path

from nb4_rf_catalog import (MAX_BUNDLE, MAX_SOURCE, CatalogError, assess_receiver,
                            import_source, merge, prepare, verify_bundle)


def bounded_read(path, limit):
    if path.stat().st_size > limit:
        raise CatalogError('Input file exceeds the permitted size.')
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise CatalogError('Input file changed or exceeds the permitted size.')
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, nargs='+',
                        help='A reviewed installation package, an ApexTX RF bundle with --check, or the source bundles with --merge.')
    parser.add_argument('--output', type=Path, help='Create a new local RF ZIP; existing files are never overwritten.')
    parser.add_argument('--check', action='store_true', help='Verify an ApexTX RF ZIP, without extracting it.')
    parser.add_argument('--assess', type=Path, help='Read-only receiver inventory JSON to assess with --check.')
    parser.add_argument('--merge', metavar='PROFILE',
                        help='Combine verified source bundles into this merged profile (for example nb4-rf-2); needs --output.')
    args = parser.parse_args()
    if args.check and args.output or args.assess and not args.check:
        parser.error('Use --output with an installation package; use --assess only with --check.')
    if args.merge and not args.output or len(args.source) > 1 and not args.merge:
        parser.error('Several sources are only combined with --merge PROFILE --output ZIP.')
    try:
        if args.merge:
            description = merge(args.source, args.output, args.merge)
        elif args.check:
            description, _ = verify_bundle(bounded_read(args.source[0], MAX_BUNDLE))
        elif args.output:
            description = prepare(args.source[0], args.output)
        else:
            description, _ = import_source(bounded_read(args.source[0], MAX_SOURCE))
        result = dict(catalog=description, status='verified-catalog-only', device_writes=False)
        if args.assess:
            inventory = json.loads(bounded_read(args.assess, 16384))
            result['receiver_assessment'] = assess_receiver(description, inventory)
        print(json.dumps(result, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
