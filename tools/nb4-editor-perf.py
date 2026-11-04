#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compare two physical NB4 captures exported by the Editor storage handover."""
import argparse
import json
import statistics
from pathlib import Path


def read(path):
    value=json.loads(Path(path).read_bytes())
    if value.get('format')!='apextx-ui-perf-1' or len(value.get('samples',[]))!=30:
        raise ValueError('Record at least 30 seconds before opening Editor.')
    # Leave out UI navigation into/out of the measured scene.
    return value,value['samples'][2:-2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline',type=Path)
    parser.add_argument('candidate',type=Path,nargs='?')
    parser.add_argument('--idle',action='store_true')
    args=parser.parse_args()
    try:
        base,rows=read(args.baseline)
        if args.idle:
            passed=all(row['pixels']==0 and row['areas']==0 for row in rows)
            report=dict(idle=passed,samples=len(rows),serial=base['serial'])
        else:
            if not args.candidate:parser.error('Provide the matching capture with a background.')
            other,next_rows=read(args.candidate)
            if (base['serial'],base['firmware'])!=(other['serial'],other['firmware']):
                raise ValueError('Compare the same transmitter and firmware build.')
            timings={}
            passed=True
            for name,field in (('ui','loopAvgUs'),('lvgl','lvglAvgUs')):
                baseline=statistics.fmean(row[field] for row in rows)
                candidate=statistics.fmean(row[field] for row in next_rows)
                if baseline<=0:raise ValueError('The baseline has no '+name+' timing samples.')
                overhead=candidate/baseline-1
                passed=passed and overhead<=.10
                timings[name]=dict(baselineMeanUs=baseline,candidateMeanUs=candidate,overhead=overhead)
            report=dict(passed=passed,timings=timings,limit=.10,serial=base['serial'],samples=len(rows))
        print(json.dumps(report,indent=2))
        raise SystemExit(0 if passed else 1)
    except (ValueError,KeyError,OSError) as error:parser.error(str(error))


if __name__=='__main__':main()
