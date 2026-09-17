#!/usr/bin/env python3
"""Step 7.6o: each system's solve time and where it goes, across node counts.

    report_scaling.py LOGS JOB [JOB...] > scaling.md

JOB is an allocation that ran `--mode external` (and, optionally,
`--mode comm_share` after it). For every (graph, node count) the table gives
each system's median solve over the held-out sources, and RIKEN's and
Gluon-Async's lead as the paired median of ACIC time / their time, so above 1
means the baseline is faster. A graph measured by two allocations at one node
count keeps the later one.

The share table reads the comm_share rows: ACIC's compute share (its own work,
less the sends made from inside it) and send share, RIKEN's time inside MPI,
and Gluon's sync time over its solve timer (both maxima over hosts, from every
external run). Each timed build's median beside its plain twin is its cost.
"""
import gzip
import json
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def load(logs, job, mode):
    path = next(Path(logs).glob(f'{mode}-*n-*w-{job}.jsonl'), None)
    if not path:
        return []
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def gluon_from_raw(logs, job):
    """(graph, nodes) -> [sync/timer] for external-phase Gluon runs, read from
    the raw log, for allocations recorded before run.py parsed the timers."""
    path = next(Path(logs).glob(f'external-*n-*w-{job}.log.gz'), None)
    out = defaultdict(list)
    if not path:
        return out
    with gzip.open(path, 'rt') as f:
        text = f.read()
    for block in text.split('\nRUN ')[1:]:
        head, _, body = block.partition('\n')
        r = json.loads(head)
        sync = re.search(r'^STAT, 0, Gluon, Sync_SSSP_0, HMAX, (\d+)', body, re.M)
        timer = re.search(r'^STAT, 0, SSSP, Timer_0, HMAX, (\d+)', body, re.M)
        if r.get('phase') == 'external' and r['config']['engine'] == 'gluon' and sync and timer:
            out[(r['graph'], r['nodes'])].append(int(sync[1]) / int(timer[1]))
    return out


def med(values):
    return statistics.median(values) if values else None


def fmt(x, unit='', digits=3):
    return '—' if x is None else f'{x:.{digits}g}{unit}'


def main():
    logs, jobs = sys.argv[1], sys.argv[2:]
    times = {}      # (graph, nodes) -> arm -> {source: seconds}
    failed = {}     # (graph, nodes) -> arm -> failed count
    gluon = {}      # (graph, nodes) -> [sync/timer]
    shares = {}     # (graph, nodes) -> arm -> [records]
    for job in jobs:
        ext = [r for r in load(logs, job, 'external') if r.get('phase') == 'external']
        cells = defaultdict(lambda: defaultdict(dict))
        fails = defaultdict(lambda: defaultdict(int))
        sync = defaultdict(list)
        for r in ext:
            key = (r['graph'], r['nodes'])
            arm = r['config']['name']
            if r.get('valid'):
                cells[key][arm][r['source']] = r['seconds']
            else:
                fails[key][arm] += 1
            if r['config']['engine'] == 'gluon' and r.get('gluon_timer_ms'):
                sync[key].append(r['gluon_sync_ms'] / r['gluon_timer_ms'])
        if not any(sync.values()):
            sync = gluon_from_raw(logs, job)
        for key in cells:
            times[key], failed[key], gluon[key] = cells[key], fails[key], sync.get(key, [])
        cs = defaultdict(lambda: defaultdict(list))
        for r in load(logs, job, 'comm_share'):
            if r.get('valid'):
                cs[(r['graph'], r['nodes'])][r['config']['name']].append(r)
        for key in cs:
            shares[key] = cs[key]

    graphs = sorted({g for g, _ in times})
    print('| graph | nodes | ACIC | RIKEN | Gluon-Async | RIKEN lead | Gluon lead | failed runs |')
    print('|---|---:|---:|---:|---:|---:|---:|---|')
    for g in graphs:
        for n in sorted(n for gg, n in times if gg == g):
            cell = times[(g, n)]
            acic = cell.get('adaptive', {})

            def lead(arm):
                other = cell.get(arm, {})
                pairs = [acic[s] / other[s] for s in acic if s in other]
                return med(pairs)
            bad = ', '.join(f'{a} {c}' for a, c in sorted(failed[(g, n)].items())) or '0'
            print(f'| {g} | {n} | {fmt(med(list(acic.values())), " s")} '
                  f'| {fmt(med(list(cell.get("riken", {}).values())), " s")} '
                  f'| {fmt(med(list(cell.get("gluon-async", {}).values())), " s")} '
                  f'| {fmt(lead("riken"), "x")} | {fmt(lead("gluon-async"), "x")} | {bad} |')
    print()
    print('| graph | nodes | ACIC compute | ACIC sends | ACIC other | timers cost ACIC | RIKEN in MPI | timers cost RIKEN | Gluon sync / solve |')
    print('|---|---:|---:|---:|---:|---:|---:|---:|---:|')
    for g, n in sorted(set(shares) | set(k for k, v in gluon.items() if v)):
        arms = shares.get((g, n), {})

        def share(arm, field):
            return med([r[field] for r in arms.get(arm, []) if field in r])

        def cost(arm):
            plain = med([r['seconds'] for r in arms.get(arm, [])])
            timed = med([r['seconds'] for r in arms.get(arm + '-timed', [])])
            return timed / plain if plain and timed else None
        pct = lambda x: '—' if x is None else f'{100 * x:.0f}%'
        print(f'| {g} | {n} | {pct(share("adaptive-timed", "comm_compute_share"))} '
              f'| {pct(share("adaptive-timed", "comm_send_share"))} '
              f'| {pct(share("adaptive-timed", "comm_other_share"))} '
              f'| {fmt(cost("adaptive"), "x")} | {pct(share("riken-timed", "mpi_share"))} '
              f'| {fmt(cost("riken"), "x")} | {pct(med(gluon.get((g, n), [])))} |')


if __name__ == '__main__':
    main()
