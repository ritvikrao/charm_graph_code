#!/usr/bin/env python3
"""Validate raw R0 results and summarize sources within each allocation.

Warmups are checked but excluded. Take the median over repetitions first,
then the median over sources; form ratios on paired source medians. Keep
allocations separate, including outliers and repeated production controls.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
from statistics import median

from check_onenode_digest import check
from onenode_report import fields
from work_cost_report import counters, production_attempts


def read_rows(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def cells(rows, expected):
    keys = [(r['variant'], str(r['source']), r['rep']) for r in rows]
    if len(keys) != len(set(keys)) or set(keys) != expected:
        raise ValueError('missing, duplicate or extra result cells')


def summarize(rows):
    output = {}
    for label in sorted({r['variant'] for r in rows}):
        selected = [r for r in rows if r['variant'] == label and r['rep'] >= 0]
        sources = {}
        for source in sorted({r['source'] for r in selected}):
            paired = [r['metrics'] for r in selected if r['source'] == source]
            if any(set(p) != set(paired[0]) for p in paired):
                raise ValueError('inconsistent metric fields')
            sources[source] = {k: median(p[k] for p in paired) for k in paired[0]}
        output[label] = dict(sources=sources, medians={
            k: median(s[k] for s in sources.values())
            for k in next(iter(sources.values()))})
    return output


def ratio(summary, numerator, denominator, metric):
    a, b = summary[numerator]['sources'], summary[denominator]['sources']
    if set(a) != set(b):
        raise ValueError('unpaired sources')
    per_source = {s: a[s][metric] / b[s][metric] for s in a}
    return dict(sources=per_source, median=median(per_source.values()))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign', type=Path)
    ap.add_argument('jobs', nargs='+', type=int)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    root = args.campaign
    result = dict(method=__doc__, campaign=str(root), allocations=[], raw_sha256={})
    checked = dict(acic=0, gap=0, diagnostic=0)
    for job in args.jobs:
        directories = sorted((root / 'logs').glob(f'AB-*-*n-{job}'))
        if len(directories) != 2:
            raise ValueError(f'{job}: expected both R0 graphs')
        for directory in directories:
            manifest = json.loads((directory / 'manifest.json').read_text())
            rows = read_rows(directory / 'runs.jsonl')
            graph = rows[0]['graph']
            refpath = root / 'graphs' / f'{graph}.reference.txt'
            references = {r[0]: r for line in refpath.read_text().splitlines()
                          if line[:1].isdigit() for r in [line.split()]}
            sources = [s for s, r in references.items()
                       if r[1] == manifest['source_role']][:manifest['sources']]
            variants = {v['label']: v for v in manifest['variants']}
            expected = {(v, s, r) for v in variants for s in sources
                        for r in range(-1, manifest['reps'])}
            cells(rows, expected)
            vertices = int(re.search(r'vertices=(\d+)',
                (root / 'graphs' / f'{graph}.meta').read_text())[1])
            all_rows = []
            gap_config = None
            for row in rows:
                source = str(row['source'])
                if row['graph'] != graph or source != sources[row['source_index']]:
                    raise ValueError('source index or graph mismatch')
                path = directory / f"{row['variant']}-s{row['source_index']}-r{row['rep']}.out"
                text = path.read_text()
                check(refpath, source, path)
                raw_times = re.findall(r'Compute time: ([\d.eE+-]+)', text)
                if len(raw_times) != 1 or float(raw_times[0]) != row['seconds'] or not row['valid']:
                    raise ValueError(f'{path}: inconsistent timing/validation')
                attempts = production_attempts(text, vertices)
                ref = references[source]
                metrics = dict(seconds=row['seconds'], edge_attempts=attempts,
                               attempts_per_edge=attempts / int(ref[7]))
                diagnostic = variants[row['variant']].get('work_cost', False)
                if diagnostic:
                    c = counters(text)
                    if c['edge_attempts'] != attempts:
                        raise ValueError(f'{path}: direct/ledger mismatch')
                    if sum(c[k] for k in ('intra_process', 'intra_node', 'inter_node')) != attempts:
                        raise ValueError(f'{path}: offered-update topology mismatch')
                    comm = fields(text, 'COMM_SHARE')
                    queue = (c['push_ticks'] + c['pop_ticks']) * c['sample_period'] * comm['window_pe_seconds'] / fields(text, 'WORK_CLOCK')['window_ticks']
                    work = comm['work_seconds'] - comm['work_send_seconds']
                    metrics.update(expansions_per_vertex=c['expansions'] / int(ref[4]),
                        changes_per_vertex=c['changes'] / int(ref[4]),
                        stale_pop_fraction=c['stale_pops'] / c['queue_pops'],
                        cas_failure_fraction=c['cas_failures'] / c['cas_attempts'],
                        lock_miss_fraction=c['lock_misses'] / c['queue_probes'],
                        queue_work_fraction=queue / work,
                        queue_window_fraction=queue / comm['window_pe_seconds'],
                        cpu_ns_per_attempt=c['cpu_ns'] / attempts,
                        work_ns_per_attempt=1e9 * work / attempts,
                        intra_process_fraction=c['intra_process'] / attempts,
                        inter_node_fraction=c['inter_node'] / attempts,
                        work_share=comm['compute_share'], idle_share=comm['idle_share'],
                        send_share=comm['send_share'], other_share=comm['other_share'])
                    checked['diagnostic'] += 1
                all_rows.append(dict(variant=row['variant'], source=source,
                                     rep=row['rep'], metrics=metrics))
                result['raw_sha256'][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
                checked['acic'] += 1
            if manifest['nodes'] == 1:
                gapdir = root / 'logs' / f'GAP-R0-{job}'
                if not (gapdir / 'complete').is_file():
                    raise ValueError(f'{gapdir}: missing completion marker')
                gaprows = [r for r in read_rows(gapdir / 'runs.jsonl') if r['graph'] == graph]
                cells(gaprows, {(v, s, r) for v in ('production', 'diagnostic')
                      for s in sources for r in range(-1, manifest['reps'])})
                gap_config = {}
                for label in ('production', 'diagnostic'):
                    configs = {(r['threads'], r['delta'], r['binary_sha256'])
                               for r in gaprows if r['variant'] == label}
                    if len(configs) != 1:
                        raise ValueError('GAPBS configuration changed within an arm')
                    threads, delta, sha = configs.pop()
                    gap_config[label] = dict(threads=threads, delta=delta, binary_sha256=sha)
                for row in gaprows:
                    source = str(row['source'])
                    index = sources.index(source)
                    path = gapdir / f"{graph}-{row['variant']}-s{index}-r{row['rep']}.out"
                    text = path.read_text()
                    check(refpath, source, path)
                    ref = references[source]
                    raw_times = re.findall(r'BENCH source=\d+ solve_seconds=([\d.eE+-]+)', text)
                    if (len(raw_times) != 1 or float(raw_times[0]) != row['seconds']
                            or not row['valid'] or row['hosts'] != manifest['hosts']
                            or row['reachable_vertices'] != int(ref[4])
                            or row['reachable_arcs'] != int(ref[7])):
                        raise ValueError(f'{path}: inconsistent timing/reference/allocation')
                    metrics = dict(seconds=row['seconds'])
                    if row['variant'] == 'diagnostic':
                        c = counters(text)
                        if c != row['counters']:
                            raise ValueError(f'{path}: raw counters disagree')
                        metrics.update(edge_attempts=c['edge_attempts'],
                            attempts_per_edge=c['edge_attempts'] / int(ref[7]),
                            expansions_per_vertex=c['expansions'] / int(ref[4]),
                            stale_pop_fraction=c['stale_pops'] / c['queue_pops'],
                            cpu_ns_per_attempt=c['cpu_ns'] / c['edge_attempts'])
                        checked['diagnostic'] += 1
                    all_rows.append(dict(variant='gap_' + row['variant'], source=source,
                                         rep=row['rep'], metrics=metrics))
                    result['raw_sha256'][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
                    checked['gap'] += 1
            for row in all_rows:
                if any(not math.isfinite(v) or v < 0 for v in row['metrics'].values()) or row['metrics']['seconds'] <= 0:
                    raise ValueError('invalid metric')
            summary = summarize(all_rows)
            comparisons = {}
            pairs = [('candidate', 'shared'), ('control', 'candidate'),
                     ('candidate_diag', 'candidate'), ('shared_diag', 'shared')]
            for a, b in pairs:
                for metric in ('seconds', 'attempts_per_edge'):
                    comparisons[f'{a}/{b}:{metric}'] = ratio(summary, a, b, metric)
            if manifest['nodes'] == 1:
                for a, b, metric in [('candidate', 'gap_production', 'seconds'),
                                     ('gap_diagnostic', 'gap_production', 'seconds'),
                                     ('candidate', 'gap_diagnostic', 'attempts_per_edge')]:
                    comparisons[f'{a}/{b}:{metric}'] = ratio(summary, a, b, metric)
            result['allocations'].append(dict(job=job, graph=graph, manifest=manifest,
                gap_config=gap_config, summary=summary, comparisons=comparisons, rows=all_rows))
    result['checked_solves'] = checked
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(checked))
    for a in result['allocations']:
        print(a['job'], a['graph'])
        for label, s in a['summary'].items():
            print(' ', label, json.dumps({k: round(v, 5) for k, v in s['medians'].items()}))
        print(' paired ratios', {k: round(v['median'], 5) for k, v in a['comparisons'].items()})


if __name__ == '__main__':
    main()
