#!/usr/bin/env python3
"""Summarize a Charm++ projections trace without the Projections GUI.

Reads <root>.sts and <root>.<pe>.log[.gz] and prints, over the traced window
(traceBegin to traceEnd):

- where PE time goes: each entry method's exclusive time, pack/unpack, idle,
  and the remainder ("untraced": the scheduler, network progress, timers and
  anything else that runs outside an entry method);
- how that time is spread across PEs (imbalance) and over the run (timeline);
- messages: count and bytes by entry method, and send-to-execute latency split
  by locality (same process, same node, other node).

Each Reconverse process starts its own clock (Cmi_startTime), so timestamps
from different processes are not comparable. Per-process offsets are
estimated NTP-style from the smallest send-to-execute delay in each direction
against process 0 and applied before cross-process latencies are computed.

usage: projections_report.py ROOT --pes-per-process 15 --processes-per-node 8
       [--jobs 64] [--bin-ms 20] [--json out.json]
"""
import argparse
import collections
import gzip
import json
import multiprocessing
import os
import re
import sys

import numpy as np

CREATION, BEGIN_PROCESSING, END_PROCESSING = 1, 2, 3
BEGIN_TRACE, END_TRACE = 11, 12
BEGIN_IDLE, END_IDLE = 14, 15
BEGIN_PACK, END_PACK, BEGIN_UNPACK, END_UNPACK = 16, 17, 18, 19
CREATION_BCAST, CREATION_MULTICAST = 20, 21

IDLE, PACK, UNPACK = -1, -2, -3
PSEUDO = {IDLE: 'idle', PACK: 'pack', UNPACK: 'unpack'}


def read_sts(root):
    entries, chares, pes = {}, {}, None
    with open(root + '.sts') as f:
        for line in f:
            if line.startswith('ENTRY CHARE '):
                m = re.match(r'ENTRY CHARE (\d+) "(.*)" (-?\d+) (-?\d+)', line)
                entries[int(m.group(1))] = (m.group(2), int(m.group(3)))
            elif line.startswith('CHARE '):
                m = re.match(r'CHARE (\d+) "(.*)" (-?\d+)', line)
                chares[int(m.group(1))] = m.group(2)
            elif line.startswith('PROCESSORS '):
                pes = int(line.split()[1])
    names = {}
    for ep, (name, chare) in entries.items():
        short = name.split('(')[0]
        names[ep] = '%s::%s' % (chares.get(chare, '?'), short)
    return names, pes


def open_log(root, pe):
    for path in ('%s.%d.log.gz' % (root, pe), '%s.%d.log' % (root, pe)):
        if os.path.exists(path):
            return gzip.open(path, 'rt') if path.endswith('.gz') else open(path)
    raise FileNotFoundError('%s.%d.log[.gz]' % (root, pe))


def parse_pe(task):
    """One PE's log -> exclusive times, timeline bins, and message arrays."""
    root, pe, bin_us = task
    excl = collections.defaultdict(float)   # key -> microseconds
    count = collections.Counter()
    t0 = t1 = None
    tracing = False
    stack = []                               # [key, start, child]
    bins = collections.defaultdict(lambda: collections.defaultdict(float))
    sent_event, sent_time, sent_ep, sent_len = [], [], [], []
    recv_src, recv_event, recv_time, recv_ep, recv_len = [], [], [], [], []
    base = None

    def add_bins(key, start, end):
        # Top-level intervals only, so bins partition wall time without overlap.
        b = int((start - base) // bin_us)
        while start < end:
            edge = base + (b + 1) * bin_us
            seg = min(end, edge) - start
            bins[key][b] += seg
            start += seg
            b += 1

    def close(expected, t):
        # Pop to the matching frame; unmatched ends (a frame opened before
        # traceBegin) are ignored rather than corrupting the stack.
        for i in range(len(stack) - 1, -1, -1):
            if stack[i][0] == expected or (expected == 'entry' and stack[i][0] >= 0):
                break
        else:
            return
        while len(stack) > i:
            key, start, child = stack.pop()
            dur = t - start
            excl[key] += dur - child
            count[key] += 1
            if stack:
                stack[-1][2] += dur
            else:
                add_bins(key, start, t)

    with open_log(root, pe) as f:
        for line in f:
            p = line.split()
            if not p or not p[0].isdigit():
                continue
            typ = int(p[0])
            if typ == BEGIN_TRACE:
                t = int(p[1])
                tracing = True
                if t0 is None:
                    t0 = t
                    base = t
                continue
            if typ == END_TRACE:
                t1 = int(p[1])
                tracing = False
                continue
            if not tracing:
                continue
            if typ == BEGIN_PROCESSING:
                ep, t = int(p[2]), int(p[3])
                stack.append([ep, t, 0])
                recv_src.append(int(p[5]))
                recv_event.append(int(p[4]))
                recv_time.append(t)
                recv_ep.append(ep)
                recv_len.append(int(p[6]))
            elif typ == END_PROCESSING:
                close('entry', int(p[3]))
            elif typ in (BEGIN_IDLE, BEGIN_PACK, BEGIN_UNPACK):
                key = {BEGIN_IDLE: IDLE, BEGIN_PACK: PACK, BEGIN_UNPACK: UNPACK}[typ]
                stack.append([key, int(p[1]), 0])
            elif typ in (END_IDLE, END_PACK, END_UNPACK):
                key = {END_IDLE: IDLE, END_PACK: PACK, END_UNPACK: UNPACK}[typ]
                close(key, int(p[1]))
            elif typ in (CREATION, CREATION_BCAST, CREATION_MULTICAST):
                sent_ep.append(int(p[2]))
                sent_time.append(int(p[3]))
                sent_event.append(int(p[4]))
                sent_len.append(int(p[6]))
    if t0 is None:
        return pe, None
    if t1 is None:
        t1 = max([t0] + recv_time + sent_time)
    arr = lambda xs, dt: np.asarray(xs, dtype=dt)
    return pe, {
        'window': t1 - t0, 't0': t0,
        'excl': dict(excl), 'count': dict(count),
        'bins': {k: dict(v) for k, v in bins.items()},
        'sent': (arr(sent_event, np.int64), arr(sent_time, np.int64),
                 arr(sent_ep, np.int32), arr(sent_len, np.int64)),
        'recv': (arr(recv_src, np.int64), arr(recv_event, np.int64),
                 arr(recv_time, np.int64), arr(recv_ep, np.int32),
                 arr(recv_len, np.int64)),
    }


def pct(xs, qs):
    return np.percentile(xs, qs) if len(xs) else [float('nan')] * len(qs)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('root', help='trace prefix, e.g. traces/acic_prj')
    ap.add_argument('--pes-per-process', type=int, required=True)
    ap.add_argument('--processes-per-node', type=int, required=True)
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    ap.add_argument('--bin-ms', type=float, default=20.0)
    ap.add_argument('--top', type=int, default=15)
    ap.add_argument('--json', help='also write the summary here')
    args = ap.parse_args()

    names, npes = read_sts(args.root)
    name = lambda k: PSEUDO.get(k) or names.get(k, 'ep%d' % k)
    bin_us = args.bin_ms * 1000.0
    ppp, ppn = args.pes_per_process, args.processes_per_node
    proc_of = lambda pe: pe // ppp
    node_of = lambda pe: pe // (ppp * ppn)

    with multiprocessing.Pool(min(args.jobs, npes)) as pool:
        per = dict(r for r in pool.imap_unordered(
            parse_pe, [(args.root, pe, bin_us) for pe in range(npes)]))
    missing = [pe for pe, d in per.items() if d is None]
    if missing:
        sys.exit('no traced window on PEs %s' % missing[:10])
    pes = sorted(per)
    nproc = proc_of(pes[-1]) + 1

    out = {'pes': len(pes), 'processes': nproc, 'nodes': node_of(pes[-1]) + 1}
    total_window = sum(per[pe]['window'] for pe in pes)
    out['window_s_mean'] = total_window / len(pes) / 1e6

    # --- where the time goes -------------------------------------------------
    excl = collections.Counter()
    count = collections.Counter()
    for pe in pes:
        excl.update(per[pe]['excl'])
        count.update(per[pe]['count'])
    traced = sum(excl.values())
    untraced = total_window - traced
    print('# Projections summary: %d PEs, %d processes, %d nodes' %
          (len(pes), nproc, out['nodes']))
    print('\nTraced window: %.3f s per PE (mean), %.1f PE-seconds in total.\n' %
          (out['window_s_mean'], total_window / 1e6))
    print('## Where PE time goes (exclusive)\n')
    print('| activity | PE-seconds | share | calls | mean |')
    print('|---|---:|---:|---:|---:|')
    rows = [(k, v) for k, v in excl.items()]
    rows.append(('untraced', untraced))
    rows.sort(key=lambda kv: -kv[1])
    out['time'] = []
    for k, v in rows:
        label = k if isinstance(k, str) else name(k)
        n = count.get(k, 0)
        mean = '%.1f us' % (v / n) if n else ''
        print('| %s | %.2f | %.1f%% | %s | %s |' %
              (label, v / 1e6, 100 * v / total_window, n or '', mean))
        out['time'].append({'activity': label, 'pe_seconds': v / 1e6,
                            'share': v / total_window, 'calls': n})

    # --- spread across PEs ----------------------------------------------------
    busy = np.array([sum(v for k, v in per[pe]['excl'].items() if k != IDLE)
                     for pe in pes]) / 1e6
    idle = np.array([per[pe]['excl'].get(IDLE, 0.0) for pe in pes]) / 1e6
    win = np.array([per[pe]['window'] for pe in pes]) / 1e6
    other = win - busy - idle
    print('\n## Spread across PEs (seconds per PE)\n')
    print('| quantity | min | p10 | median | p90 | max |')
    print('|---|---:|---:|---:|---:|---:|')
    out['spread'] = {}
    for label, xs in (('busy (entries+pack)', busy), ('idle', idle),
                      ('untraced', other), ('window', win)):
        q = pct(xs, [0, 10, 50, 90, 100])
        print('| %s | %s |' % (label, ' | '.join('%.3f' % x for x in q)))
        out['spread'][label] = list(map(float, q))
    by_proc = collections.defaultdict(float)
    for pe, b in zip(pes, busy):
        by_proc[proc_of(pe)] += b
    pb = np.array([by_proc[i] for i in range(nproc)])
    print('\nBusy time by process: min %.2f, median %.2f, max %.2f PE-s '
          '(max/median %.2f).' % (pb.min(), np.median(pb), pb.max(),
                                  pb.max() / max(np.median(pb), 1e-9)))
    out['busy_by_process'] = pb.tolist()

    # --- timeline -------------------------------------------------------------
    nb = int(max(win) * 1e6 // bin_us) + 1
    keys = set()
    for pe in pes:
        keys.update(per[pe]['bins'])
    series = {k: np.zeros(nb) for k in keys}
    for pe in pes:
        for k, d in per[pe]['bins'].items():
            for b, v in d.items():
                if b < nb:
                    series[k][b] += v
    cap = len(pes) * bin_us
    top = sorted((k for k in keys if k != IDLE),
                 key=lambda k: -series[k].sum())[:4]
    print('\n## Timeline (share of all PE time per %.0f ms bin; top-level)\n' %
          args.bin_ms)
    print('| t (s) | idle | untraced | %s |' % ' | '.join(name(k) for k in top))
    print('|---:|---:|---:|%s' % ('---:|' * len(top)))
    stride = max(1, nb // 40)
    out['timeline'] = []
    for b in range(0, nb, stride):
        sl = slice(b, min(nb, b + stride))
        denom = cap * (sl.stop - sl.start)
        tot = sum(series[k][sl].sum() for k in keys)
        vals = [series[k][sl].sum() / denom for k in top]
        idle_b = series[IDLE][sl].sum() / denom if IDLE in series else 0.0
        print('| %.2f | %.2f | %.2f | %s |' % (
            b * bin_us / 1e6, idle_b, 1 - tot / denom,
            ' | '.join('%.2f' % v for v in vals)))
        out['timeline'].append([b * bin_us / 1e6, idle_b, 1 - tot / denom] + vals)

    # --- messages -------------------------------------------------------------
    s_key, s_time, s_ep, s_len, s_pe = [], [], [], [], []
    r_key, r_time, r_ep, r_len, r_pe, r_src = [], [], [], [], [], []
    for pe in pes:
        ev, t, ep, ln = per[pe]['sent']
        s_key.append((np.int64(pe) << 32) | ev)
        s_time.append(t); s_ep.append(ep); s_len.append(ln)
        s_pe.append(np.full(len(ev), pe, np.int64))
        src, ev, t, ep, ln = per[pe]['recv']
        r_key.append((src << 32) | ev)
        r_time.append(t); r_ep.append(ep); r_len.append(ln)
        r_pe.append(np.full(len(ev), pe, np.int64)); r_src.append(src)
    cat = np.concatenate
    s_key, s_time, s_ep, s_len, s_pe = map(cat, (s_key, s_time, s_ep, s_len, s_pe))
    r_key, r_time, r_ep, r_len, r_pe, r_src = map(cat, (r_key, r_time, r_ep, r_len, r_pe, r_src))

    print('\n## Messages sent in the window, by target entry method\n')
    print('| entry method | messages | MB | mean bytes |')
    print('|---|---:|---:|---:|')
    out['sent'] = []
    for ep in sorted(set(s_ep.tolist()), key=lambda e: -int((s_ep == e).sum()))[:args.top]:
        m = s_ep == ep
        n, b = int(m.sum()), int(s_len[m].sum())
        print('| %s | %d | %.1f | %.0f |' % (name(ep), n, b / 1e6, b / n))
        out['sent'].append({'ep': name(ep), 'messages': n, 'bytes': b})

    # Match each execution to its creation (same source PE and event id).
    order = np.argsort(s_key, kind='stable')
    sk = s_key[order]
    idx = np.searchsorted(sk, r_key)
    ok = (idx < len(sk))
    ok[ok] = sk[idx[ok]] == r_key[ok]
    ok &= r_src >= 0
    src_pe = r_src[ok]
    dst_pe = r_pe[ok]
    raw = (r_time[ok] - s_time[order][idx[ok]]).astype(np.float64)
    sp, dp = proc_of(src_pe), proc_of(dst_pe)

    # Clock offsets: offset[p] = clock(p) - clock(0), from the minimum delay
    # each way between p and every process whose offset is already known.
    offset = np.full(nproc, np.nan)
    offset[0] = 0.0
    mins = {}
    for a in range(nproc):
        for b in range(nproc):
            if a != b:
                m = (sp == a) & (dp == b)
                if m.any():
                    mins[(a, b)] = raw[m].min()
    for _ in range(nproc):
        for p in range(nproc):
            if not np.isnan(offset[p]):
                continue
            est = [((mins[(q, p)] - mins[(p, q)]) / 2.0) + offset[q]
                   for q in range(nproc)
                   if not np.isnan(offset[q]) and (q, p) in mins and (p, q) in mins]
            if est:
                offset[p] = np.median(est)
    offset = np.nan_to_num(offset)
    lat = raw - (offset[dp] - offset[sp])
    locality = np.where(sp == dp, 0, np.where(node_of(src_pe) == node_of(dst_pe), 1, 2))

    print('\n## Send-to-execute latency (ms), matched %d of %d executions\n' %
          (int(ok.sum()), len(r_key)))
    print('Process clock offsets against process 0: %s ms.\n' %
          ', '.join('%.2f' % (o / 1e3) for o in offset))
    print('| entry method | locality | messages | p50 | p90 | p99 | max |')
    print('|---|---|---:|---:|---:|---:|---:|')
    out['latency'] = []
    r_ep_ok = r_ep[ok]
    for ep in sorted(set(r_ep_ok.tolist()), key=lambda e: -int((r_ep_ok == e).sum()))[:args.top]:
        for loc, label in ((0, 'same process'), (1, 'same node'), (2, 'other node')):
            m = (r_ep_ok == ep) & (locality == loc)
            if m.sum() < 10:
                continue
            q = pct(lat[m] / 1e3, [50, 90, 99, 100])
            print('| %s | %s | %d | %s |' % (name(ep), label, int(m.sum()),
                                             ' | '.join('%.2f' % x for x in q)))
            out['latency'].append({'ep': name(ep), 'locality': label,
                                   'messages': int(m.sum()),
                                   'p50_p90_p99_max_ms': list(map(float, q))})
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(out, f, indent=1)


if __name__ == '__main__':
    main()
