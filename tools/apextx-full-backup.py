#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Create a verified private MCU/NOR backup using the read-only RAM agent."""
import argparse
import json
from pathlib import Path
from nb4_backup_agent import full_backup as run, verify




if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--agent', type=Path)
    parser.add_argument('--agent-sha256')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--serial')
    parser.add_argument('--verify', type=Path)
    args = parser.parse_args()
    if args.verify and any((args.agent, args.agent_sha256, args.output, args.serial)):
        parser.error('--verify cannot be combined with USB backup options.')
    if not args.verify and not all((args.agent, args.agent_sha256, args.output)):
        parser.error('Supply the verified agent, its SHA-256 and a new output directory.')
    try:
        result = verify(args.verify) if args.verify else run(args.agent, args.agent_sha256, args.output, args.serial,
            lambda message, percent: print(f'{percent:3d}% {message}', flush=True))
        print(json.dumps(dict(format=result['format'], serial=result['identity']['serial'],
                              verified=True, nonvolatile_writes=False), indent=2))
        if not args.verify:
            print('Backup complete. The RAM agent is switching off. Disconnect USB, then start the radio normally. No firmware was replaced.')
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f'Error: {error}\n')
