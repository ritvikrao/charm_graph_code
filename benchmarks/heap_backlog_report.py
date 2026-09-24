#!/usr/bin/env python3
"""Count outstanding self heap-drain callbacks in a Projections trace.

Creation and execution are on the same PE, so this needs no clock alignment.
The count excludes the currently executing callback. Report every PE, not
only an average that could hide duplicate drain chains on a few workers.
"""
import argparse
import concurrent.futures
import json
import statistics

from projections_report import open_log, read_sts


def scan(task):
    root, pe, heap, threshold = task
    pending, at_start, at_threshold, delays = {}, [], [], []
    maximum, tracing = 0, False
    with open_log(root, pe) as stream:
        for line in stream:
            a = line.split()
            if not a or not a[0].isdigit():
                continue
            typ = int(a[0])
            if typ == 11:
                tracing = True
            elif typ == 12:
                tracing = False
            elif tracing and typ == 1 and int(a[2]) == heap:
                event = int(a[4])
                if event in pending:
                    raise ValueError('duplicate creation event on PE %d' % pe)
                pending[event] = int(a[3])
                maximum = max(maximum, len(pending))
            elif tracing and typ == 2 and int(a[2]) == heap and int(a[5]) == pe:
                created = pending.pop(int(a[4]), None)
                if created is not None:
                    delays.append(int(a[3]) - created)
                    at_start.append(len(pending))
            elif tracing and typ == 2 and int(a[2]) == threshold:
                at_threshold.append(len(pending))
    if not at_start or not at_threshold:
        raise ValueError('missing heap or threshold events on PE %d' % pe)
    return dict(pe=pe, max_pending=maximum,
                median_pending_at_execution=statistics.median(at_start),
                median_pending_at_threshold=statistics.median(at_threshold),
                executed=len(at_start), median_delay_us=statistics.median(delays),
                max_delay_us=max(delays), pending_at_end=len(pending))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root')
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--require-single', action='store_true')
    ap.add_argument('--json')
    args = ap.parse_args()
    names, pes = read_sts(args.root)
    heap = next(k for k, v in names.items() if v == 'SsspChares::process_heap')
    threshold = next(k for k, v in names.items() if v == 'SsspChares::current_thresholds')
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
        rows = list(pool.map(scan, [(args.root, pe, heap, threshold) for pe in range(pes)]))
    result = dict(pes=pes, max_pending=max(r['max_pending'] for r in rows),
                  duplicate_pes=sum(r['max_pending'] > 1 for r in rows), per_pe=rows)
    if args.json:
        with open(args.json, 'w') as stream:
            json.dump(result, stream, indent=2)
            stream.write('\n')
    print('HEAP_BACKLOG pes=%d max_pending=%d duplicate_pes=%d' %
          (pes, result['max_pending'], result['duplicate_pes']))
    if args.require_single and result['max_pending'] > 1:
        raise SystemExit('coalescing failed: multiple pending heap callbacks')


if __name__ == '__main__':
    main()
