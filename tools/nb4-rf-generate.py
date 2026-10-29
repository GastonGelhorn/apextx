#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate/check the firmware view of the shared RF image catalog."""
import argparse
from pathlib import Path
from nb4_rf_catalog import firmware_catalog_header

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    path = Path(__file__).resolve().parents[1] / 'radio/src/nb4_rf_images.h'
    content = firmware_catalog_header()
    if args.check:
        if not path.exists() or path.read_text() != content:
            parser.exit(1, 'RF catalog header is stale. Run tools/nb4-rf-generate.py.\n')
    else:
        path.write_text(content)
