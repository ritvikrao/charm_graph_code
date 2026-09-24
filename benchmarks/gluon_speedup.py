#!/usr/bin/env python3
"""ACIC's speedup over the distributed baselines from gluon_compare.sbatch jobs.

For each job and graph: per held-out source, the median over repetitions of
each baseline arm (run.py external records, phase 'external': gluon-async,
gluon-sync, riken) and of each ACIC arm (onenode_ab.py runs.jsonl, rep >= 0).
Speedup = baseline time / ACIC time. `best` is the faster Gluon mode on that
source; RIKEN is reported on its own. Every ACIC arm is reported against
`frozen` (production acic_slice) when both ran. A baseline accepted under
run.py --riken-tolerance is marked inexact with its largest relative
distance-sum error.

  gluon_speedup.py CAMPAIGN JOB [JOB ...] [--json OUT]
"""
import argparse, json, statistics
from collections import defaultdict
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('campaign', type=Path)
ap.add_argument('jobs', nargs='+')
ap.add_argument('--json', type=Path)
args = ap.parse_args()
logs = args.campaign / 'logs'
result = {}
fmt = lambda x, p=3: '-' if x is None else f'{x:.{p}f}'
for job in args.jobs:
    base = defaultdict(list)
    inexact = defaultdict(float)
    for path in logs.glob(f'external-*n-56w-{job}.jsonl'):
        for line in path.open():
            r = json.loads(line)
            if r.get('phase') == 'external':
                if not r.get('valid'):
                    raise SystemExit(f'invalid baseline run in {path}: {r["config"]["name"]} {r["source"]}')
                base[r['graph'], str(r['source']), r['config']['name']].append(r['seconds'])
                if r.get('exact') is False:
                    key = r['graph'], r['config']['name']
                    inexact[key] = max(inexact[key], r['relative_distance_error'])
    acic = defaultdict(list)
    for d in logs.glob(f'AB-*n-{job}'):
        for line in (d / 'runs.jsonl').open():
            r = json.loads(line)
            if r['rep'] >= 0 and r['valid']:
                acic[r['graph'], r['source'], r['variant']].append(r['seconds'])
    for graph in sorted({g for g, _, _ in base} | {g for g, _, _ in acic}):
        med = lambda table, s, arm: statistics.median(table[graph, s, arm]) if table.get((graph, s, arm)) else None
        baselines = sorted({a for g, _, a in base if g == graph})
        arms = sorted({a for g, _, a in acic if g == graph}, key=lambda a: (a != 'frozen', a))
        sources = sorted({s for g, s, _ in base if g == graph} | {s for g, s, _ in acic if g == graph})
        rows = []
        for s in sources:
            row = dict(source=s, **{b: med(base, s, b) for b in baselines}, **{a: med(acic, s, a) for a in arms})
            gluon = [row[b] for b in baselines if b.startswith('gluon') and row[b]]
            row['gluon_best'] = min(gluon) if gluon else None
            for a in arms:
                if not row[a]:
                    continue
                for b in [b for b in baselines if not b.startswith('gluon')] + ['gluon_best']:
                    if row[b]:
                        row[f'speedup_{a}_over_{b}'] = row[b] / row[a]
                if a != 'frozen' and row.get('frozen'):
                    row[f'speedup_{a}_over_frozen'] = row['frozen'] / row[a]
            rows.append(row)
        sel = sorted(logs.glob(f'external-*n-56w-{job}-{graph}-selected.json'))
        chosen = {a['name']: a['chosen'] for a in json.loads(sel[0].read_text())['arms']} if sel else {}
        errors = {b: e for (g, b), e in inexact.items() if g == graph}
        result[f'{graph}@{job}'] = dict(job=job, graph=graph, baselines_selected=chosen, sources=rows,
                                        inexact_max_relative_error=errors)
        print(f'{graph} job {job}  selected: {chosen}')
        for b, e in sorted(errors.items()):
            print(f'  {b} INEXACT: distance sum high by up to {e:.2e} (run.py --riken-tolerance)')
        for r in rows:
            times = ' '.join(f'{k}={fmt(r[k])}' for k in baselines + ['gluon_best'] + arms)
            speed = ' '.join(f'{k[8:]}={fmt(v, 2)}x' for k, v in r.items() if k.startswith('speedup_'))
            print(f"  {r['source']:>10} {times}\n  {'':>10} {speed}")
        for k in sorted({k for r in rows for k in r if k.startswith('speedup_')}):
            v = [r[k] for r in rows if r.get(k)]
            if len(v) == len(rows):
                print(f'  range {k[8:]}: {min(v):.2f}-{max(v):.2f}x')
if args.json:
    args.json.write_text(json.dumps(result, indent=2) + '\n')
