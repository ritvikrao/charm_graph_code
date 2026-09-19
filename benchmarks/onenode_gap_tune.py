#!/usr/bin/env python3
"""Joint GAPBS thread/delta tuning using only designated training sources.

Preserves the external harness's records, digest checks, timing and upstream
bucket fusion. Boundary winners are explicit: they do not prove a global optimum.
"""
import argparse
import json
import random
import re
import statistics
from run import Campaign


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign')
    ap.add_argument('--graphs', default='road-usa-z,mesh24-z,mesh26-z')
    ap.add_argument('--threads', default='1,2,4,8,16,32,64,96,120,127,128')
    ap.add_argument('--sources', type=int, default=4)
    ap.add_argument('--reps', type=int, default=3)
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--launch-timeout', type=int, default=60,
                    help='wall-clock cap per tuning launch, including startup and graph loading')
    ap.add_argument('--selection-job', help='reuse a frozen selection in a second allocation')
    args = ap.parse_args()
    if args.sources < 1 or args.reps < 1 or args.launch_timeout < 1:
        ap.error('sources, repetitions and launch timeout must be positive')
    args.mode, args.workers = 'external', 120
    args.ablation_binary, args.per_graph_width = 'acic', 'off'
    campaign = Campaign(args)
    if campaign.nodes != 1:
        ap.error('GAPBS tuning requires one node')
    threads = sorted(set(map(int, args.threads.split(','))))
    if not threads or min(threads) < 1 or max(threads) > 128:
        ap.error('thread counts must be between 1 and 128')
    rng = random.Random(20260919 + int(campaign.job))
    for graph in args.graphs.split(','):
        refs = campaign.references(graph)
        train = [r for r in refs if r['role'] == 'tune']
        test = [r for r in refs if r['role'] == 'test'][:args.sources]
        if len(train) < 2 or len(test) != args.sources:
            raise ValueError(f'{graph}: insufficient training/held-out sources')
        selection_path = campaign.root/'logs'/f'{campaign.tag}-{graph}-selected.json'
        if args.selection_job:
            prior = campaign.root/'logs'/f'external-1n-120w-{args.selection_job}-{graph}-selected.json'
            selection = json.loads(prior.read_text())
            winner = selection['arms'][0]
        else:
            meta = (campaign.root/'graphs'/f'{graph}.meta').read_text()
            denominator = int(re.search(r'\briken_denominator=(\d+)', meta)[1])
            deltas = sorted({max(1, denominator // d) for d in [256, 64, 16, 4, 1]}
                            | {4 * denominator, 16 * denominator})
            candidates = [dict(engine='gap', name=f'gap-t{t}-d{d}',
                               threads=t, cpus=t, delta=d, launch_timeout_seconds=args.launch_timeout)
                          for t in threads for d in deltas]
            campaign.run(graph, int(train[0]['source']), candidates[0], train[0], 'external-warmup')
            samples = {c['name']: [] for c in candidates}
            order = [(c, r) for c in candidates for r in train]
            rng.shuffle(order)
            for c, row in order:
                samples[c['name']].append(campaign.run(graph, int(row['source']), c, row, 'external-joint'))
            valid = [c for c in candidates if all(r['valid'] for r in samples[c['name']])]
            if not valid:
                raise RuntimeError(f'{graph}: no valid GAPBS candidate')
            winner = min(valid, key=lambda c: statistics.geometric_mean(r['seconds'] for r in samples[c['name']]))
            # Repeat the top three on training data to reduce selection noise.
            finalists = sorted(valid, key=lambda c: statistics.geometric_mean(r['seconds'] for r in samples[c['name']]))[:3]
            for rep in range(2):
                order = [(c, r) for c in finalists for r in train]
                rng.shuffle(order)
                for c, row in order:
                    samples[c['name']].append(campaign.run(graph, int(row['source']), c, row, 'external-confirm', rep))
            finalists = [c for c in finalists if all(r['valid'] for r in samples[c['name']])]
            if not finalists:
                raise RuntimeError(f'{graph}: all finalists failed confirmation')
            winner = min(finalists, key=lambda c: statistics.geometric_mean(r['seconds'] for r in samples[c['name']]))
            selection = dict(arms=[winner], search=dict(kind='joint', threads=threads, deltas=deltas,
                boundary_delta=winner['delta'] in (deltas[0], deltas[-1]),
                boundary_threads=winner['threads'] in (threads[0], threads[-1]),
                candidates={name: dict(valid=all(r['valid'] for r in runs), seconds=[r.get('seconds') for r in runs])
                            for name, runs in samples.items()}))
        selection_path.write_text(json.dumps(selection, indent=2) + '\n')
        for row in test:
            warm = campaign.run(graph, int(row['source']), winner, row, 'external-test-warmup')
            if not warm['valid']:
                raise RuntimeError(f'{graph}: held-out warmup failed validation')
        for rep in range(args.reps):
            order = test[:]
            rng.shuffle(order)
            for row in order:
                run = campaign.run(graph, int(row['source']), winner, row, 'external', rep)
                if not run['valid']:
                    raise RuntimeError(f'{graph}: held-out run failed validation')


if __name__ == '__main__':
    main()
