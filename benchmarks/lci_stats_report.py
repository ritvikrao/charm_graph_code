#!/usr/bin/env python3
"""Summarize the LCI_STATS lines an instrumented libreconverse prints at exit.

    lci_stats_report.py RUN.out [RUN.out ...] [--tsc-hz 2.45e9]

One line per PE thread: active messages sent, how many sends had to retry and
how many extra post attempts that took, and time (TSC ticks) spent inside the
send call, including its retries; progress() calls, how many found the device
lock taken by another thread, and time inside LCI's progress. The report gives
totals and the share of PE time those represent, using Compute time x PEs as
the denominator (the counts cover the whole run, so the shares are upper
bounds for the solve).
"""
import argparse
import re
import statistics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--tsc-hz', type=float, default=2.45e9)
    args = ap.parse_args()
    for path in args.files:
        text = open(path, errors='replace').read()
        keys = ('am', 'am_bytes', 'am_loops', 'am_iters', 'am_tsc', 'prog',
                'prog_busy', 'prog_done', 'prog_tsc', 'get', 'get_iters',
                'get_tsc', 'why_retry', 'why_lock', 'why_nopacket', 'why_nomem',
                'why_backlog')
        # Threads print concurrently, so a line can be cut by another's; keep
        # only complete records, one per (rank, thread).
        rows = {}
        # Records from before the retry reasons were added lack the why_ keys.
        if 'why_backlog=' not in text:
            keys = keys[:12]
        for m in re.finditer(r'LCI_STATS rank=(\d+) thread=(\d+) dev=(-?\d+)'
                             + ''.join(r' %s=(\d+)' % k for k in keys), text):
            rows[(m.group(1), m.group(2))] = dict(
                zip(keys, (int(x) for x in m.groups()[3:])))
        rows = list(rows.values())
        m = re.search(r'^Compute time: ([\d.]+)', text, re.M)
        compute = float(m.group(1)) if m else None
        if not rows:
            print('%s: no LCI_STATS' % path)
            continue
        tot = {k: sum(r[k] for r in rows) for k in rows[0]}
        pe_s = compute * len(rows) if compute else None
        hz = args.tsc_hz
        print('## %s\n' % path)
        print('%d threads, compute %s s' % (len(rows), compute))
        print('| quantity | total | per thread (median) | share of PE time |')
        print('|---|---:|---:|---:|')

        def line(name, key, secs=False):
            vals = [r[key] for r in rows]
            if secs:
                t = tot[key] / hz
                share = '%.1f%%' % (100 * t / pe_s) if pe_s else ''
                print('| %s | %.2f s | %.3f s | %s |' % (
                    name, t, statistics.median(vals) / hz, share))
            else:
                print('| %s | %d | %d | |' % (name, tot[key], statistics.median(vals)))
        line('sends (post_am)', 'am')
        print('| bytes sent | %.2f GB | | |' % (tot['am_bytes'] / 1e9))
        line('sends that retried', 'am_loops')
        line('extra post attempts', 'am_iters')
        line('time inside send calls', 'am_tsc', True)
        line('progress() calls', 'prog')
        line('... device lock busy', 'prog_busy')
        line('... returned work done', 'prog_done')
        line('time inside LCI progress', 'prog_tsc', True)
        for k in keys[12:]:
            line('retries: ' + k[4:], k)
        line('RDMA gets posted', 'get')
        line('time inside get calls', 'get_tsc', True)
        print()


if __name__ == '__main__':
    main()
