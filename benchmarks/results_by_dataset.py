#!/usr/bin/env python3
"""Every held-out Frontier result, grouped by dataset, node count and
implementation, as the markdown in design/current-state.md.

Reads the raw campaign logs of the jobs listed below: ACIC runs from
logs/AB-*-<job>/runs.jsonl (variant labels mapped to implementations; ablation,
control and work-cost arms are left out), distributed baselines from
logs/external-<N>n-56w-<job>.jsonl (phase 'external'), one-node GAPBS and Wasp
from logs/external-1n-*-<job>.jsonl. Only valid solves count, except RIKEN on
the OSM roads, which is inexact (run.py --riken-tolerance) and is marked.

Time: per held-out source, the median over repetitions and allocations; the
table gives the range over sources. Speedup = baseline time / ACIC time per
source, range over sources. Where ACIC and a baseline ran in the same job, the
speedup is paired within each job and the ranges are joined; otherwise
(one-node references, and the 64-node cells whose ACIC arms were rerun
separately) per-source medians are paired across jobs. Gluon is the faster of
Async and Sync per source where both ran.

  results_by_dataset.py CAMPAIGN [--json OUT]
"""
import argparse, glob, json, re, statistics
from collections import defaultdict
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('campaign', type=Path)
ap.add_argument('--json', type=Path)
args = ap.parse_args()
L = args.campaign / 'logs'

PROD = 'ACIC production'            # acic_slice, 5ab6d5b
TLS_H2 = 'ACIC TLS (acic_hint2_tls)'  # hints v2 working tree, initial-exec TLS runtime
TLS = 'ACIC TLS (acic_tls)'          # 7afd94d, initial-exec TLS runtime
W64 = 'ACIC production (acic_w64m)'  # 88aa0c3, WIRE=compact64 (ids past 2^31)
TLS64 = 'ACIC TLS (acic_tls_w64m)'   # 88aa0c3, WIRE=compact64, initial-exec TLS runtime
HEAP69 = 'ACIC TLS, heap queue (acic_heap69)'          # 69adfc1, initial-exec TLS runtime
CHUNKS = 'ACIC TLS, chunk queue, slice 64 (acic_c256)'  # 69adfc1, ACIC_PROCESS_CHUNKS band 256
# job -> {variant label: implementation}; the layout is part of the name when
# it is not 8 processes x 7 workers per node.
ACIC_JOBS = {
    '5536321': 'scaling', '5536322': 'scaling', '5538412': 'scaling', '5538413': 'scaling',
    '5534022': {'n8_8x7_s8': PROD, 'n16_8x7_s8': PROD}, '5534023': {'n8_8x7_s8': PROD, 'n16_8x7_s8': PROD},
    '5538405': {'n8_w128k': PROD, 'n16_w128k': PROD}, '5538406': {'n8_w128k': PROD, 'n16_w128k': PROD},
    '5538389': {'n16_4x14': PROD}, '5538391': {'n16_4x14': PROD},
    '5538390': {'n16_8x7': PROD}, '5538392': {'n16_8x7': PROD},
}
for j in ['5539286', '5539899', '5539900', '5539985', '5541240', '5541195', '5541196']:
    ACIC_JOBS[j] = {'frozen': PROD, 'h2tls': TLS_H2}
for j in ['5541369', '5541370', '5541660', '5541426', '5541428', '5541663', '5541664', '5541665', '5541667']:
    ACIC_JOBS[j] = {'frozen': PROD, 'tls': TLS}
for j in ['5546135', '5546136', '5546137']:  # road-planet-z
    ACIC_JOBS[j] = {'frozen': PROD, 'tls': TLS}
for j in ['5546130', '5546131']:  # terrain-ae-z
    ACIC_JOBS[j] = {'w64': W64, 'tls64': TLS64}
ACIC_JOBS['5548095'] = {'heap': HEAP69, 'c256_s64': CHUNKS}  # one-node mesh28-z queue A/B
EXTERNAL_JOBS = ['5536474', '5536475', '5536476', '5539286', '5539899', '5539900', '5539985',
                 '5541240', '5541195', '5541196', '5541369', '5541370', '5541371', '5541428', '5541429',
                 '5541663', '5541664', '5541665', '5541667', '5541713', '5541714', '5541715', '5541716', '5541717',
                 '5546135', '5546136', '5546137']
ONE_NODE_JOBS = ['5529591', '5538465', '5538410', '5541358', '5541661',
                 '5536541', '5538411', '5541359', '5541662']
DATASETS = ['mesh24-z', 'mesh26-z', 'mesh28-z', 'mesh30-z', 'road-usa-z', 'road-na-z', 'road-eu-z',
            'road-planet-z', 'terrain-ae-z',
            'orkut', 'uniform25', 'rmat25', 'rmat26', 'rmat27']
BASELINES = ['GAPBS', 'Wasp', 'Gluon', 'RIKEN']


def lines(path):
    for line in open(path):
        try:
            yield json.loads(line)
        except ValueError:
            pass

# (graph, nodes, implementation) -> job -> source -> [seconds]
acic = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
layout = {}
for job, labels in ACIC_JOBS.items():
    for d in glob.glob(str(L / f'AB-*-{job}')):
        manifest = json.load(open(d + '/manifest.json'))
        rpn = {v['label']: v['rpn'] for v in manifest['variants']}
        for r in lines(d + '/runs.jsonl'):
            if r['rep'] < 0 or not r['valid']:
                continue
            label = r['variant']
            if labels == 'scaling':
                impl = PROD if re.fullmatch(r'n\d+_s8', label) else None
            else:
                impl = labels.get(label)
            if not impl:
                continue
            if rpn[label] != 8:
                impl += f', {rpn[label]} × {56 // rpn[label]}'
            acic[r['graph'], r['nodes'], impl][job][str(r['source'])].append(r['seconds'])

base = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))  # (graph, nodes, baseline) -> job -> source
gluon_modes = defaultdict(lambda: defaultdict(lambda: defaultdict(lambda: defaultdict(list))))
inexact = defaultdict(list)
settings = defaultdict(set)
for job in EXTERNAL_JOBS + ONE_NODE_JOBS:
    for p in glob.glob(str(L / f'external-*n-*w-{job}.jsonl')):
        nodes = int(re.search(r'external-(\d+)n', p)[1])
        for r in lines(p):
            if r['phase'] != 'external' or not r['valid']:
                continue
            c, g, s = r['config'], r['graph'], str(r['source'])
            engine = c['engine']
            name = {'gap': 'GAPBS', 'wasp': 'Wasp', 'riken': 'RIKEN', 'gluon': 'Gluon'}[engine]
            if engine == 'gluon':
                gluon_modes[g, nodes][job][c['name']][s].append(r['seconds'])
            else:
                base[g, nodes, name][job][s].append(r['seconds'])
            if r.get('exact') is False:
                inexact[g, nodes].append(r['relative_distance_error'])
            chosen = c.get('chosen', c['name'])
            settings[g, nodes, name].add(chosen)
# Gluon: the faster mode per source, per job.
for (g, nodes), jobs in gluon_modes.items():
    for job, modes in jobs.items():
        for s in set().union(*[set(v) for v in modes.values()]):
            meds = [statistics.median(v[s]) for v in modes.values() if v.get(s)]
            base[g, nodes, 'Gluon'][job][s] = [min(meds)]
        for m in modes:
            settings[g, nodes, 'Gluon'].add(m)


def per_source(jobs):
    pooled = defaultdict(list)
    for per in jobs.values():
        for s, v in per.items():
            pooled[s] += v
    return {s: statistics.median(v) for s, v in pooled.items()}


def num(x):
    return f'{x:.3f}' if x < 1 else f'{x:.2f}' if x < 10 else f'{x:.1f}' if x < 100 else f'{x:.0f}'


def ratio(x):
    return f'{x:.2f}' if x < 10 else f'{x:.1f}' if x < 100 else f'{x:.0f}'


def rng(values, suffix='', fmt=num):
    if not values:
        return '—'
    lo, hi = fmt(min(values)), fmt(max(values))
    return f'{lo}{suffix}' if lo == hi else f'{lo}–{hi}{suffix}'


def speedup(acic_jobs, base_jobs):
    shared = set(acic_jobs) & set(base_jobs)
    ratios = []
    if shared:
        for job in shared:
            a, b = per_source({job: acic_jobs[job]}), per_source({job: base_jobs[job]})
            ratios += [b[s] / a[s] for s in a if s in b]
    else:
        a, b = per_source(acic_jobs), per_source(base_jobs)
        ratios = [b[s] / a[s] for s in a if s in b]
    return ratios, bool(shared)


def jobs_text(jobs):
    return ', '.join(sorted(jobs))


meta = {}
for g in DATASETS:
    m = dict(re.findall(r'(\w+)=(\d+)', (args.campaign / 'graphs' / f'{g}.meta').read_text()))
    meta[g] = (int(m['vertices']), int(m['arcs']))

out, record = [], {}
for g in DATASETS:
    n, m = meta[g]
    out.append(f'#### `{g}` ({n / 1e6:,.1f}M vertices, {m / 1e6:,.1f}M edges)\n')
    out.append('| Nodes | Implementation | Time per solve (s) | Sources | Jobs |')
    out.append('|---:|---|---:|---:|---|')
    rows = []
    for (gg, nodes, impl), jobs in acic.items():
        if gg == g:
            rows.append((nodes, 0 if impl.startswith(PROD) else 0.5, impl, jobs))
    for (gg, nodes, b), jobs in base.items():
        if gg == g:
            rows.append((nodes, 1 + BASELINES.index(b), b, jobs))
    for nodes, _, impl, jobs in sorted(rows, key=lambda r: (r[0], r[1], r[2])):
        t = per_source(jobs)
        label = impl
        if impl in BASELINES:
            chosen = sorted(settings[g, nodes, impl])
            if impl == 'Gluon':
                label = 'Gluon (' + ', '.join(sorted({c.split('-')[1].capitalize() for c in chosen})) + ')'
            if impl == 'RIKEN' and inexact.get((g, nodes)):
                label = 'RIKEN, inexact'
        out.append(f'| {nodes} | {label} | {rng(list(t.values()))} | {len(t)} | {jobs_text(jobs)} |')
        record.setdefault(g, []).append(dict(nodes=nodes, implementation=impl, seconds=t, jobs=sorted(jobs)))
    out.append('')
    # Speedups of each ACIC build at each node count over every baseline.
    table = []
    for (gg, nodes, impl), jobs in sorted(acic.items(), key=lambda kv: (kv[0][1], not kv[0][2].startswith(PROD), kv[0][2])):
        if gg != g:
            continue
        cells = []
        for b in BASELINES:
            bnodes = 1 if b in ('GAPBS', 'Wasp') else nodes
            if (g, bnodes, b) not in base:
                cells.append('—')
                continue
            ratios, paired = speedup(jobs, base[g, bnodes, b])
            cells.append(rng(ratios, '×', ratio) if ratios else '—')
        if any(c != '—' for c in cells):
            table.append(f'| {nodes} | {impl} | ' + ' | '.join(cells) + ' |')
    if table:
        out.append("ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):\n")
        out.append('| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |')
        out.append('|---:|---|---:|---:|---:|---:|')
        out += table
        out.append('')
    errors = sorted({round(e, 7) for (gg, _), v in inexact.items() if gg == g for e in v})
    if errors:
        out.append(f'RIKEN on `{g}` reaches every vertex but its distance sum is high by '
                   f'{min(errors):.1e}–{max(errors):.1e} (relative).\n')
print('\n'.join(out))
if args.json:
    args.json.write_text(json.dumps(record, indent=1) + '\n')
