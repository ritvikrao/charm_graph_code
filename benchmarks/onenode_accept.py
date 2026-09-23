#!/usr/bin/env python3
"""Fail-closed paper acceptance, with explicit paired allocation IDs.

Each --allocation is ACIC_8_JOB,GAP_1_JOB,ACIC_1_JOB,REGRESSION_8_JOB.
All selected cells need four held-out sources and three repetitions. Regression
noise is the largest absolute log-ratio of the repeated baseline's source
medians in that allocation. No missing cell, input, or second allocation passes.
"""
import argparse
import json
import math
from pathlib import Path
import statistics


def references(root, graph, sources):
    rows = [r.split() for r in (root/'graphs'/f'{graph}.reference.txt').read_text().splitlines()
            if r[:1].isdigit()]
    result = [int(r[0]) for r in rows if r[1] == 'test'][:sources]
    if len(result) != sources:
        raise ValueError(f'{graph}: insufficient held-out sources')
    return result


def medians(rows, sources, reps):
    samples = {}
    for r in rows:
        if r['rep'] < 0:
            continue
        key = (int(r['source']), r['rep'])
        if key in samples or not r['valid'] or not math.isfinite(r['seconds']) or r['seconds'] <= 0:
            raise ValueError('duplicate, invalid, or nonpositive timing')
        samples[key] = r['seconds']
    expected = {(s, r) for s in sources for r in range(reps)}
    if samples.keys() != expected:
        raise ValueError('missing or extra source/repetition cells')
    return {s: statistics.median(samples[s, r] for r in range(reps)) for s in sources}


def acic(root, graph, nodes, job, label, sources, reps):
    path = root/'logs'/f'AB-{graph}-{nodes}n-{job}'
    if not (path/'summary.json').exists():
        raise ValueError(f'{path.name}: incomplete comparison')
    manifest = json.loads((path/'manifest.json').read_text())
    if (manifest.get('source_role') != 'test' or manifest['nodes'] != nodes
            or manifest['sources'] != len(sources) or manifest['reps'] != reps):
        raise ValueError(f'{path.name}: wrong role/layout/sample counts')
    variant = next(v for v in manifest['variants'] if v['label'] == label)
    if variant.get('graph', graph) != graph:
        raise ValueError('acceptance requires original graph IDs from the reader path')
    rows = [json.loads(line) for line in (path/'runs.jsonl').read_text().splitlines()]
    timing = medians([r for r in rows if r['variant'] == label], sources, reps)
    identity = (variant['sha256'], tuple(variant.get('flags', [])), manifest['workers'], manifest['rpn'])
    return timing, identity


def gap(root, graph, job, sources, reps):
    path = root/'logs'/f'external-1n-120w-{job}.jsonl'
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    rows = [r for r in rows if r['graph'] == graph and r['phase'] == 'external'
            and r['config']['engine'] == 'gap']
    identity = {(r['binary_sha256'], r['config']['threads'], r['config']['delta']) for r in rows}
    if len(identity) != 1:
        raise ValueError(f'{graph}: mixed or missing GAPBS configuration')
    return medians(rows, sources, reps), identity.pop()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign', type=Path)
    ap.add_argument('--allocation', action='append', required=True)
    ap.add_argument('--variant', default='candidate')
    ap.add_argument('--graphs', default='road-usa-z,road-usa-w4-z,mesh24-z,mesh26-z')
    ap.add_argument('--regressions', default='rmat25,orkut,uniform25,rmat26,rmat27')
    ap.add_argument('--sources', type=int, default=4)
    ap.add_argument('--reps', type=int, default=3)
    ap.add_argument('--regression-reps', type=int, help='timed launches per regression arm (default --reps)')
    args = ap.parse_args()
    allocations = [a.split(',') for a in args.allocation]
    if len(allocations) != 2 or any(len(a) != 4 for a in allocations):
        ap.error('supply exactly two allocations, each with four comma-separated job IDs')
    args.regression_reps = args.regression_reps or args.reps
    if args.sources < 4 or args.reps < 3 or args.regression_reps < 3:
        ap.error('acceptance requires at least four held-out sources and three repetitions')
    if any(a == b for a, b in zip(*allocations)):
        ap.error('the two allocations must use distinct jobs for every role')
    root = args.campaign
    output, identities, baselines, frozen_hashes = [], set(), {}, set()
    try:
        for number, (eight_job, gap_job, one_job, regression_job) in enumerate(allocations, 1):
            for graph in args.graphs.split(','):
                sources = references(root, graph, args.sources)
                eight, identity = acic(root, graph, 8, eight_job, args.variant, sources, args.reps)
                one, one_identity = acic(root, graph, 1, one_job, args.variant, sources, args.reps)
                identities.update([identity, one_identity])
                baseline, baseline_identity = gap(root, graph, gap_job, sources, args.reps)
                if graph in baselines and baselines[graph] != baseline_identity:
                    raise ValueError(f'{graph}: GAPBS selection differs between allocations')
                baselines[graph] = baseline_identity
                ratios = [eight[s] / baseline[s] for s in sources]
                own_ratios = [eight[s] / one[s] for s in sources]
                passed = statistics.median(ratios) <= 1 and max(ratios) <= 1.2 and statistics.median(own_ratios) <= 1
                output.append(dict(allocation=number, graph=graph, gap_ratio=statistics.median(ratios),
                    worst_gap_ratio=max(ratios), own_one_node_ratio=statistics.median(own_ratios), passed=passed))
            for graph in args.regressions.split(','):
                sources = references(root, graph, args.sources)
                candidate, identity = acic(root, graph, 8, regression_job, args.variant, sources, args.regression_reps)
                identities.add(identity)
                frozen, frozen_identity = acic(root, graph, 8, regression_job, 'frozen', sources, args.regression_reps)
                control, control_identity = acic(root, graph, 8, regression_job, 'control', sources, args.regression_reps)
                if frozen_identity != control_identity:
                    raise ValueError('repeated control differs from frozen configuration')
                frozen_hashes.add(frozen_identity)
                floor = math.exp(max(abs(math.log(control[s] / frozen[s])) for s in sources))
                ratio = max(candidate[s] / frozen[s] for s in sources)
                output.append(dict(allocation=number, graph=graph, worst_regression_ratio=ratio,
                                   observed_allocation_floor=floor, passed=ratio <= floor))
        if len(identities) != 1:
            raise ValueError('candidate binary, flags, or per-node layout differ between cells')
        if len(frozen_hashes) != 1:
            raise ValueError('frozen regression configuration differs between allocations')
    except (ValueError, KeyError, StopIteration, FileNotFoundError) as error:
        print(json.dumps(dict(status='INCOMPLETE', reason=str(error), cells=output), indent=2))
        raise SystemExit(2)
    passed = all(r['passed'] for r in output)
    print(json.dumps(dict(status='PASS' if passed else 'NO-GO', cells=output), indent=2))
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
