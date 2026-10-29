#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline component planning and explicitly confirmed integrated maintenance."""
import argparse
import json
from pathlib import Path
from nb4_integrated_update import IntegratedUpdate,COMPONENTS

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',type=Path,required=True)
    parser.add_argument('--create',type=Path,metavar='EXTRACTED_PACKAGE')
    parser.add_argument('--serial')
    parser.add_argument('--target',choices=('nb4','nb4plus'))
    parser.add_argument('--confirm-unit')
    parser.add_argument('--component',choices=('application','module','receiver','resources','audio'))
    parser.add_argument('--select',nargs='+',choices=COMPONENTS,help='Replace the reviewed selection; omitted components are explicitly deferred.')
    parser.add_argument('--path',type=Path,help='Backup, audio pack or mounted storage root for the selected action.')
    parser.add_argument('--action',choices=('status','read','backup','attach-backup','approve','storage','audio-pack',
        'stage','install','cancel','recover','restore-nor','restore-application','restore-audio'),default='status')
    parser.add_argument('--reinstall',action='store_true')
    args=parser.parse_args()
    try:
        plan=IntegratedUpdate.create(args.create,args.serial,{'nb4':1,'nb4plus':2}.get(args.target),args.session) if args.create else IntegratedUpdate.load(args.session)
        if args.select is not None: plan.select({name:name in args.select for name in COMPONENTS})
        if args.action in ('approve','install','recover','restore-nor','restore-application','restore-audio') and args.confirm_unit!=plan.record['serial']:
            raise RuntimeError('Review the plan, then confirm the exact transmitter serial with --confirm-unit.')
        progress=lambda message,percent: print(f'{percent:3d}% {message}',flush=True)
        if args.action=='read': plan.inventory()
        elif args.action=='backup': plan.backup(progress)
        elif args.action=='attach-backup': plan.attach_backup(args.path)
        elif args.action=='approve': plan.approve()
        elif args.action=='storage': plan.prepare_storage()
        elif args.action=='audio-pack': plan.add_audio(args.path)
        elif args.action=='restore-nor': plan.restore_nor(progress)
        elif args.action=='restore-application': plan.restore_application(progress)
        elif args.action=='restore-audio': plan.restore_audio(args.path,progress)
        elif args.action in ('stage','install','cancel','recover'):
            if args.component in ('module','receiver'): plan.rf(args.component,args.action,progress,reinstall=args.reinstall)
            elif args.action!='install': raise RuntimeError('Select an RF component for this action.')
            elif args.component=='application': plan.application(progress)
            elif args.component=='resources': plan.resources(args.path,progress)
            elif args.component=='audio': plan.audio(args.path,progress)
            else: raise RuntimeError('Select a component.')
        print(json.dumps(plan.snapshot(),indent=2))
    except (OSError,ValueError,RuntimeError) as error: parser.exit(1,f'Error: {error}\n')
