#!/usr/bin/env python3
"""Summarize paired evidence; incomplete allocations never count as a pass."""
import argparse
import collections
import json
import statistics
from pathlib import Path


def paired(directory):
    manifest = json.loads((directory / 'manifest.json').read_text())
    rows = [json.loads(line) for line in (directory / 'runs.jsonl').read_text().splitlines()]
    runs = {(r['variant'], r['source_index'], r['rep']): r for r in rows if r['rep'] >= 0}
    base = [r for r in rows if r['variant'] == 'frozen' and r['rep'] >= 0]
    if not base:
        return []
    graph = base[0]['graph']
    reference = directory.parents[1] / 'graphs' / f'{graph}.reference.txt'
    roles = {row[0]: row[1] for line in reference.read_text().splitlines()
             if line[:1].isdigit() for row in [line.split()]}
    observed_roles = {roles[str(r['source'])] for r in base}
    role = next(iter(observed_roles)) if len(observed_roles) == 1 else 'mixed'
    result = []
    for variant in manifest['variants']:
        label = variant['label']
        if label == 'frozen': continue
        ratios = collections.defaultdict(list)
        missing = 0
        for ref in base:
            candidate = runs.get((label, ref['source_index'], ref['rep']))
            if not candidate or not candidate['valid'] or not ref['valid']:
                missing += 1
                continue
            ratios[ref['source_index']].append(candidate['seconds'] / ref['seconds'])
        if not ratios: continue
        by_source = {s: statistics.median(values) for s, values in ratios.items()}
        result.append(dict(directory=str(directory), graph=graph, nodes=manifest['nodes'],
            variant=label, role=role, sources=len(ratios), min_reps=min(map(len, ratios.values())),
            ratio=statistics.median(by_source.values()), worst=max(by_source.values()),
            missing=missing, completed=(directory / 'summary.json').exists(),
            sha256=variant['sha256']))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign', type=Path)
    args = parser.parse_args()
    results = []
    for path in sorted((args.campaign / 'logs').glob('AB-*/manifest.json')):
        results.extend(paired(path.parent))
    print('| allocation / graph | nodes | variant | role | sources | min reps | variant/frozen | worst source | complete |')
    print('|' + '---|' * 9)
    for r in results:
        print(f"| {Path(r['directory']).name} | {r['nodes']} | {r['variant']} | {r['role']} | {r['sources']} | "
              f"{r['min_reps']} | {r['ratio']:.3f} | {r['worst']:.3f} | "
              f"{'yes' if r['completed'] and not r['missing'] else 'NO'} |")
    print('\nRatios below 1 are faster. Control is a second frozen run and measures '
          'allocation variability. These are lever comparisons, not the paper pass test. '
          'Adoption requires two completed allocations with the same candidate hash, '
          'the required held-out sources, and scale-free regression checks. The paper '
          'pass additionally requires eight-node ACIC / tuned one-node GAPBS median '
          '<= 1.0 and every held-out source <= 1.2 in both allocations.')


if __name__ == '__main__':
    main()
