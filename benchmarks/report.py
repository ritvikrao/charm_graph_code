#!/usr/bin/env python3
"""Generate auditable, paired pilot tables from run.py's JSONL records."""
import argparse
from collections import defaultdict
import csv
import gzip
import json
import math
from pathlib import Path
import random
import statistics


def geometric(values):
    return math.exp(statistics.mean(math.log(x) for x in values))


def paired(base, variant):
    sources = sorted(set(base) & set(variant))
    ratios = [base[s]/variant[s] for s in sources]
    if not ratios:
        return None
    rng = random.Random(20260913)
    logs = [math.log(x) for x in ratios]
    boot = sorted(math.exp(sum(rng.choices(logs, k=len(logs)))/len(logs)) for _ in range(10000))
    return geometric(ratios), boot[249], boot[9749], len(sources)


def summarize(records):
    observations = defaultdict(list)
    for r in records:
        if r['phase'] == 'test':
            if not r['valid'] or r.get('seconds', 0) <= 0:
                raise ValueError(f'Invalid test run in job {r["job"]}; no silent exclusion')
            observations[(r['nodes'], r['workers'], r['graph'], r['config']['name'], r['source'])].append(r['seconds'])
    cells = defaultdict(dict)
    for (nodes, workers, graph, name, source), values in observations.items():
        cells[(nodes, workers, graph)].setdefault(name, {})[source] = geometric(values)
    return cells, observations


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('logs', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--allow-partial', action='store_true', help='progress view only; do not publish incomplete cells')
    args = parser.parse_args()
    records = []
    for path in sorted(args.logs.glob('benchmark-*.jsonl')):
        job_records = [json.loads(line) for line in path.read_text().splitlines()]
        if not args.allow_partial:
            job = path.stem.rsplit('-', 1)[1]
            stdout = (args.logs/f'job-{job}.out').read_text()
            if 'CAMPAIGN COMPLETE '+path.stem not in stdout:
                raise ValueError(f'Job {job} did not complete; use --allow-partial only for progress views')
            grouped = defaultdict(lambda: defaultdict(list))
            for r in job_records:
                if r['phase'] == 'test':
                    grouped[r['graph']][r['config']['name']].append(r)
            for graph, variants in grouped.items():
                selected = json.loads((args.logs/f'{path.stem}-{graph}-selected.json').read_text())
                if set(variants) != {c['name'] for c in selected}:
                    raise ValueError(f'Missing variants in {job}/{graph}')
                counts = []
                for runs in variants.values():
                    by_source = defaultdict(int)
                    for r in runs:
                        by_source[r['source']] += 1
                    counts.append(dict(by_source))
                if any(c != counts[0] for c in counts) or set(counts[0].values()) != {2} or len(counts[0]) != 8:
                    raise ValueError(f'Expected eight paired sources and two repeats in {job}/{graph}')
        records.extend(job_records)
    cells, observations = summarize(records)
    args.output.mkdir(parents=True, exist_ok=True)
    with gzip.open(args.output/'runs.jsonl.gz', 'wt') as f:
        for r in records:
            f.write(json.dumps(r, sort_keys=True)+'\n')
    names = ['current', 'old-fixed', 'tuned-fixed', 'open', 'riken', 'gap']
    table = ['| Nodes | Workers/node | Graph | Sources | ACIC current (s) | Old fixed (s) | Tuned fixed (s) | Relaxed admission (s) | RIKEN (s) | GAPBS (s) | RIKEN / ACIC [95% CI] |',
             '|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---|']
    controls = ['| Nodes | Workers/node | Graph | Duplicate / current [95% CI] | Tuned fixed / current [95% CI] | Old fixed / current |',
                '|---:|---:|---|---|---|---:|']
    rows = []
    for (nodes, workers, graph), values in sorted(cells.items()):
        current = values.get('current', {})
        if not current:
            continue
        estimates = {name: geometric(values[name].values()) for name in names if name in values}
        def ratio(name):
            result = paired(values.get(name, {}), current)
            return '—' if result is None else f'{result[0]:.2f} [{result[1]:.2f}, {result[2]:.2f}]'
        table.append(f'| {nodes} | {workers} | {graph} | {len(current)} | ' +
                     ' | '.join(f'{estimates[n]:.4f}' if n in estimates else '—' for n in names) +
                     f' | {ratio("riken")} |')
        controls.append(f'| {nodes} | {workers} | {graph} | {ratio("control")} | {ratio("tuned-fixed")} | {ratio("old-fixed")} |')
        row = dict(nodes=nodes, workers=workers, graph=graph, sources=len(current), **estimates)
        for name in ['riken', 'gap', 'control', 'tuned-fixed', 'old-fixed', 'open']:
            result = paired(values.get(name, {}), current)
            if result:
                row.update({name+'_ratio': result[0], name+'_ci_low': result[1], name+'_ci_high': result[2]})
        rows.append(row)
    (args.output/'table.md').write_text('\n'.join(table)+'\n')
    (args.output/'controls.md').write_text('\n'.join(controls)+'\n')
    keys = list(dict.fromkeys(key for row in rows for key in row))
    with (args.output/'summary.csv').open('w') as f:
        writer = csv.DictWriter(f, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)
    print(f'{len(records)} records; {len(cells)} graph/node/occupancy cells')
