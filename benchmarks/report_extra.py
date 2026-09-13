#!/usr/bin/env python3
"""Report independently allocated baseline, layout, and diagnostic probes."""
from collections import defaultdict
import gzip
import json
from pathlib import Path
import sys
from report import geometric, paired

logs, out = map(Path, sys.argv[1:3])
out.mkdir(parents=True, exist_ok=True)
records, complete, unfinished = [], [], []
completed_jobs = set()
for pattern in ['gluon-*.jsonl', 'confirm-*.jsonl', 'presolve-*.jsonl', 'numa-*.jsonl', 'finish-*.jsonl', 'layout_confirm-*.jsonl', 'quiet-*.jsonl', 'finish_layout-*.jsonl']:
    for path in sorted(logs.glob(pattern)):
        runs = [json.loads(line) for line in path.read_text().splitlines()]
        runs = [dict(r, phase='layout-test', original_phase=r['phase'], comparison_job=str(r['selection_job']))
                if r['phase']=='layout-completion' else r for r in runs]
        records += runs
        job = path.stem.rsplit('-', 1)[1]
        stdout = (logs/f'job-{job}.out').read_text()
        if 'CAMPAIGN COMPLETE '+path.stem in stdout:
            complete += runs
            completed_jobs.add(job)
        else:
            unfinished.append(dict(job=job, records=len(runs), invalid=sum(not r['valid'] for r in runs)))
resumed = {r['comparison_job'] for r in complete if r.get('original_phase')=='layout-completion'}
complete += [r for r in records if r['job'] in resumed and r['job'] not in completed_jobs]
for attempt in unfinished:
    if attempt['job'] in resumed:
        attempt['remaining_slots_accounted_for_in_completion_job'] = True
with gzip.open(out/'extra-runs.jsonl.gz', 'wt') as f:
    for r in records:
        f.write(json.dumps(r, sort_keys=True)+'\n')
(out/'extra-incomplete-attempts.json').write_text(json.dumps(unfinished, indent=2)+'\n')
(out/'extra-failed-test-runs.json').write_text(json.dumps(
    [r for r in records if r['phase'].endswith('-test') and not r['valid'] and not r.get('skipped')], indent=2)+'\n')


def source_means(phase):
    observed = defaultdict(list)
    failed = set()
    for r in complete:
        if r['phase'] == phase:
            cell_key = (r.get('comparison_job',r['job']),r['nodes'],r['workers'],r['graph'])
            if not r['valid']:
                if phase != 'layout-test':
                    raise ValueError('Invalid held-out result')
                failed.add((*cell_key,r['config']['name']))
            observed[(*cell_key,r['config']['name'],r['source'])].append(r.get('seconds',1.0))
    cells = defaultdict(dict)
    for (job, nodes, workers, graph, name, source), times in observed.items():
        if len(times) != 2:
            raise ValueError('Expected two repeats')
        cells[(job, nodes, workers, graph)].setdefault(name, {})[source] = geometric(times)
    for cell in cells.values():
        expected = {
            'gluon-test': {'current', 'control', 'gluon-async', 'gluon-sync'},
            'confirm-test': {'current', 'control', 'tuned-fixed', 'riken', 'riken-retuned'},
            'numa-test': {f'{p}-{r}x{120//r}' for p in ['current', 'fixed'] for r in [1, 4, 8]},
            'layout-test': {'current', 'control', 'tuned-fixed', 'riken', 'gap'},
            'quiet-test': {'current', 'quiet', 'riken', 'gap'},
        }[phase]
        if set(cell) != expected:
            raise ValueError('Missing supplemental variants')
        sets = [set(v) for v in cell.values()]
        if any(s != sets[0] for s in sets) or len(sets[0]) != 8:
            raise ValueError('Expected eight matched sources')
    for *key, name in failed:
        cells[tuple(key)][name] = None
    return cells


def ratio(values, name):
    if values[name] is None or values['current'] is None:
        return '—'
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

table = ['| Physical nodes | Graph | Policy | 1 × 120 (s) | 4 × 30 (s) | 8 × 15 (s) | 1 × 120 / 4 × 30 [95% CI] | 1 × 120 / 8 × 15 [95% CI] |',
         '|---:|---|---|---:|---:|---:|---|---|']
for (job, nodes, workers, graph), cell in sorted(source_means('numa-test').items()):
    for policy in ['current', 'fixed']:
        names = [f'{policy}-{r}x{120//r}' for r in [1, 4, 8]]
        ratios = [paired(cell[names[0]], cell[n]) for n in names[1:]]
        table.append(f'| {nodes} | {graph} | {policy} | '+
                     ' | '.join(f'{geometric(cell[n].values()):.4f}' for n in names)+' | '+
                     ' | '.join(f'{r[0]:.2f} [{r[1]:.2f}, {r[2]:.2f}]' for r in ratios)+' |')
(out/'numa.md').write_text('\n'.join(table)+'\n')

table = ['| Physical nodes | Graph | ACIC 8 × 15 (s) | Transferred fixed 8 × 15 (s) | RIKEN (s) | GAPBS (s) | RIKEN / ACIC [95% CI] | GAPBS / ACIC [95% CI] | Control / ACIC [95% CI] |',
         '|---:|---|---:|---:|---:|---:|---|---|---|']
for (job, nodes, workers, graph), cell in sorted(source_means('layout-test').items()):
    table.append(f'| {nodes} | {graph} | '+
                 ' | '.join('FAIL' if cell[n] is None else f'{geometric(cell[n].values()):.4f}' for n in ['current','tuned-fixed','riken','gap'])+' | '+
                 ' | '.join(ratio(cell,n) for n in ['riken','gap','control'])+' |')
(out/'layout-confirmation.md').write_text('\n'.join(table)+'\n')

table = ['| Graph | Default ACIC (s) | Quiet ACIC (s) | RIKEN (s) | GAPBS (s) | Default / quiet [95% CI] | RIKEN / quiet [95% CI] | GAPBS / quiet [95% CI] |',
         '|---|---:|---:|---:|---:|---|---|---|']
for (job, nodes, workers, graph), cell in sorted(source_means('quiet-test').items()):
    ratios = [paired(cell[n],cell['quiet']) for n in ['current','riken','gap']]
    table.append(f'| {graph} | '+
                 ' | '.join(f'{geometric(cell[n].values()):.4f}' for n in ['current','quiet','riken','gap'])+' | '+
                 ' | '.join(f'{r[0]:.2f} [{r[1]:.2f}, {r[2]:.2f}]' for r in ratios)+' |')
(out/'quiet.md').write_text('\n'.join(table)+'\n')

replays = defaultdict(list)
for r in complete:
    if r['phase'] == 'failure-replay':
        replays[(r['job'], r['graph'], r['source'], r['config']['name'])].append(r)
table = ['| Job | Graph | Source | Configuration | Valid / attempted | Successful-query geomean (s), diagnostic only |',
         '|---|---|---:|---|---:|---:|']
for (job, graph, source, name), runs in sorted(replays.items()):
    good = [r['seconds'] for r in runs if r['valid']]
    timing = f'{geometric(good):.4f}' if good else '—'
    table.append(f'| {job} | {graph} | {source} | {name} | {len(good)}/{len(runs)} | {timing} |')
(out/'failure-replay.md').write_text('\n'.join(table)+'\n')

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
