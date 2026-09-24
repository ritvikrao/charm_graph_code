#!/usr/bin/env python3
"""ACIC's speedup over Gluon from gluon_compare.sbatch jobs.

For each job and graph: per held-out source, the median over repetitions of
each Gluon mode (run.py external records, phase 'external') and of each ACIC
arm (onenode_ab.py runs.jsonl, rep >= 0). Speedup = Gluon time / ACIC time,
against the faster Gluon mode on that source; h2tls/frozen is frozen / h2tls.

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
for job in args.jobs:
    gluon = defaultdict(list)
    for path in logs.glob(f'external-*n-56w-{job}.jsonl'):
        for line in path.open():
            r = json.loads(line)
            if r.get('phase') == 'external':
                if not r.get('valid'):
                    raise SystemExit(f'invalid Gluon run in {path}: {r["config"]["name"]} {r["source"]}')
                gluon[r['graph'], str(r['source']), r['config']['name']].append(r['seconds'])
    acic = defaultdict(list)
    for d in logs.glob(f'AB-*-16n-{job}'):
        for line in (d / 'runs.jsonl').open():
            r = json.loads(line)
            if r['rep'] >= 0 and r['valid']:
                acic[r['graph'], r['source'], r['variant']].append(r['seconds'])
    graphs = sorted({g for g, _, _ in gluon} | {g for g, _, _ in acic})
    for graph in graphs:
        sources = sorted({s for g, s, _ in gluon if g == graph} | {s for g, s, _ in acic if g == graph})
        rows = []
        for s in sources:
            med = lambda table, arm: statistics.median(table[graph, s, arm]) if table.get((graph, s, arm)) else None
            row = dict(source=s, **{f'gluon_{m}': med(gluon, f'gluon-{m}') for m in ('async', 'sync')},
                       **{a: med(acic, a) for a in ('frozen', 'h2tls')})
            best = min((row[k] for k in ('gluon_async', 'gluon_sync') if row[k]), default=None)
            row['gluon_best'] = best
            for a in ('frozen', 'h2tls'):
                row[f'speedup_{a}'] = best / row[a] if best and row[a] else None
            row['h2tls_over_frozen'] = row['frozen'] / row['h2tls'] if row['frozen'] and row['h2tls'] else None
            rows.append(row)
        sel = logs / f'external-16n-56w-{job}-{graph}-selected.json'
        chosen = {a['name']: a['chosen'] for a in json.loads(sel.read_text())['arms']} if sel.exists() else {}
        result[f'{graph}@{job}'] = dict(job=job, graph=graph, gluon_selected=chosen, sources=rows)
        f = lambda x, p=3: '-' if x is None else f'{x:.{p}f}'
        rng = lambda k: '-' if not all(r[k] for r in rows) else f"{min(r[k] for r in rows):.2f}-{max(r[k] for r in rows):.2f}x"
        print(f'{graph} job {job}  Gluon selected: {chosen}')
        for r in rows:
            print(f"  {r['source']:>10} async={f(r['gluon_async'])} sync={f(r['gluon_sync'])} frozen={f(r['frozen'])} "
                  f"h2tls={f(r['h2tls'])}  speedup frozen={f(r['speedup_frozen'],2)}x h2tls={f(r['speedup_h2tls'],2)}x "
                  f"h2tls/frozen={f(r['h2tls_over_frozen'],2)}x")
        print(f"  range: speedup frozen {rng('speedup_frozen')}, h2tls {rng('speedup_h2tls')}, h2tls over frozen {rng('h2tls_over_frozen')}")
if args.json:
    args.json.write_text(json.dumps(result, indent=2) + '\n')
