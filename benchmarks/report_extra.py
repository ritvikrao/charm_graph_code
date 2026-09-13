#!/usr/bin/env python3
"""Report independently allocated Gluon, confirmation, and presolve probes."""
from collections import defaultdict
import gzip
import json
from pathlib import Path
import sys
from report import geometric, paired

logs, out = map(Path, sys.argv[1:3])
out.mkdir(parents=True, exist_ok=True)
records, complete, unfinished = [], [], []
for pattern in ['gluon-*.jsonl', 'confirm-*.jsonl', 'presolve-*.jsonl']:
    for path in sorted(logs.glob(pattern)):
        runs = [json.loads(line) for line in path.read_text().splitlines()]
        records += runs
        job = path.stem.rsplit('-', 1)[1]
        stdout = (logs/f'job-{job}.out').read_text()
        if 'CAMPAIGN COMPLETE '+path.stem in stdout:
            complete += runs
        else:
            unfinished.append(dict(job=job, records=len(runs), invalid=sum(not r['valid'] for r in runs)))
with gzip.open(out/'extra-runs.jsonl.gz', 'wt') as f:
    for r in records:
        f.write(json.dumps(r, sort_keys=True)+'\n')
(out/'extra-incomplete-attempts.json').write_text(json.dumps(unfinished, indent=2)+'\n')


def source_means(phase):
    observed = defaultdict(list)
    for r in complete:
        if r['phase'] == phase:
            if not r['valid']:
                raise ValueError('Invalid held-out result')
            observed[(r['job'], r['nodes'], r['workers'], r['graph'], r['config']['name'], r['source'])].append(r['seconds'])
    cells = defaultdict(dict)
    for (job, nodes, workers, graph, name, source), times in observed.items():
        if len(times) != 2:
            raise ValueError('Expected two repeats')
        cells[(job, nodes, workers, graph)].setdefault(name, {})[source] = geometric(times)
    for cell in cells.values():
        sets = [set(v) for v in cell.values()]
        if any(s != sets[0] for s in sets) or len(sets[0]) != 8:
            raise ValueError('Expected eight matched sources')
    return cells


def ratio(values, name):
    r = paired(values[name], values['current'])
    return f'{r[0]:.2f} [{r[1]:.2f}, {r[2]:.2f}]'


table = ['| Nodes | Workers/node | Graph | ACIC (s) | Gluon-Async (s) | Gluon-Sync (s) | Async / ACIC [95% CI] | Sync / ACIC [95% CI] | Control / ACIC [95% CI] |',
         '|---:|---:|---|---:|---:|---:|---|---|---|']
for (job, nodes, workers, graph), cell in sorted(source_means('gluon-test').items()):
    table.append(f'| {nodes} | {workers} | {graph} | '+
                 ' | '.join(f'{geometric(cell[n].values()):.4f}' for n in ['current', 'gluon-async', 'gluon-sync'])+
                 ' | '+' | '.join(ratio(cell,n) for n in ['gluon-async', 'gluon-sync', 'control'])+' |')
(out/'gluon.md').write_text('\n'.join(table)+'\n')

table = ['| Job | Nodes | Graph | ACIC (s) | Tuned fixed (s) | Frozen RIKEN (s) | Retuned RIKEN (s) | Control / ACIC [95% CI] |',
         '|---|---:|---|---:|---:|---:|---:|---|']
for (job, nodes, workers, graph), cell in sorted(source_means('confirm-test').items()):
    table.append(f'| {job} | {nodes} | {graph} | '+
                 ' | '.join(f'{geometric(cell[n].values()):.4f}' for n in ['current', 'tuned-fixed', 'riken', 'riken-retuned'])+
                 ' | '+ratio(cell,'control')+' |')
(out/'confirmation.md').write_text('\n'.join(table)+'\n')

table = ['| Graph | Sources | RIKEN no presolve (s) | RIKEN with presolve (s) | Added presolve time (s) | Solve speedup | Approx. queries to amortize |',
         '|---|---:|---:|---:|---:|---:|---:|']
by_graph = defaultdict(list)
for r in complete:
    if r['phase'] == 'presolve-probe':
        if not r['valid']:
            raise ValueError('Invalid preprocessing probe')
        by_graph[r['graph']].append(r)
for graph, runs in sorted(by_graph.items()):
    before = [r for r in runs if r['config']['presolve'] == 0]
    after = [r for r in runs if r['config']['presolve'] > 0]
    base = geometric(r['seconds'] for r in before)
    variant = geometric(r['seconds'] for r in after)
    setup = sum(r['presolve_seconds'] for r in after)/len(after)
    amortize = f'{setup/(base-variant):.0f}' if base > variant else '—'
    table.append(f'| {graph} | {len(after)} | {base:.4f} | {variant:.4f} | {setup:.2f} | {base/variant:.2f} | {amortize} |')
(out/'presolve.md').write_text('\n'.join(table)+'\n')
print(f'{len(complete)} completed supplemental records; {len(unfinished)} incomplete attempts retained')
