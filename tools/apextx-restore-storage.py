#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Experimental complete NOR restore from a verified private full-unit backup."""
import argparse
import json
from pathlib import Path
from nb4_nor_restore import run

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--agent',type=Path,required=True)
    parser.add_argument('--agent-sha256',required=True)
    parser.add_argument('--backup',type=Path,required=True)
    parser.add_argument('--operation',type=Path,required=True)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--confirm-unit',required=True)
    args=parser.parse_args()
    if args.confirm_unit != args.serial: parser.error('Confirm the exact transmitter serial.')
    try:
        print(json.dumps(run(args.agent,args.agent_sha256,args.backup,args.operation,args.serial,
              confirmed=True,progress=lambda message,percent: print(f'{percent:3d}% {message}',flush=True)),indent=2))
    except (OSError,ValueError,RuntimeError) as error:
        parser.exit(1,f'Error: {error}\n')
