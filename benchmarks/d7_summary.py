#!/usr/bin/env python3
"""D7 summary: ACIC arms (onenode_ab runs.jsonl) against GAPBS and Wasp
(onenode_gap_tune held-out records) from one d7_compare.sbatch allocation.

  d7_summary.py CAMPAIGN GRAPH COMPARE_JOB [OUT.json]

Per held-out source: median seconds of every arm, and each baseline's time
over each ACIC arm's (above 1 means ACIC is faster). Fails on any invalid solve.
"""
import json, statistics, sys
from pathlib import Path
root, graph, job = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
acic = [json.loads(l) for l in (root/'logs'/f'AB-{graph}-1n-{job}'/'runs.jsonl').open()]
ext = [json.loads(l) for l in (root/'logs'/f'external-1n-120w-{job}.jsonl').open()]
assert all(r['valid'] for r in acic), 'invalid ACIC solve'
assert all(r['valid'] for r in ext if r['graph'] == graph), 'invalid baseline solve'
sources = []
for r in acic:
    if str(r['source']) not in sources: sources.append(str(r['source']))
arms = {}
for r in acic:
    if r['rep'] >= 0: arms.setdefault(r['variant'], {}).setdefault(str(r['source']), []).append(r['seconds'])
for r in ext:
    if r['graph'] == graph and r['phase'] == 'external':
        arms.setdefault(r['config']['engine'], {}).setdefault(str(r['source']), []).append(r['seconds'])
med = {a: {s: statistics.median(v[s]) for s in sources} for a, v in arms.items()}
sel = {}
for suffix, engine in [('', 'gap'), ('-wasp', 'wasp')]:
    p = root/'logs'/f'external-1n-120w-{job}-{graph}-selected{suffix}.json'
    if p.exists(): sel[engine] = json.loads(p.read_text())['arms'][0]['name']
out = dict(graph=graph, job=job, sources=sources, selections=sel,
           acic_solves=len(acic), baseline_solves=sum(1 for r in ext if r['graph'] == graph),
           median_seconds=med, speedup={})
print(f"{graph} job {job}: {len(acic)} ACIC + {out['baseline_solves']} baseline solves, all valid; selections {sel}")
for a in med:
    print(f"  {a:18s} " + ' '.join(f"{med[a][s]:.3f}" for s in sources))
for b in ('gap', 'wasp'):
    if b not in med: continue
    for a in med:
        if a in ('gap', 'wasp'): continue
        sp = [med[b][s] / med[a][s] for s in sources]
        out['speedup'][f'{a}_over_{b}'] = sp
        print(f"  {a} vs {b}: {min(sp):.2f}-{max(sp):.2f}x")
if len(sys.argv) > 4: Path(sys.argv[4]).write_text(json.dumps(out, indent=1) + '\n')
