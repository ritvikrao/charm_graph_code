#!/usr/bin/env python3
"""Tune and time GAPBS on one node, inside the same allocation as ACIC arms.

Training sources select threads and delta from a grid centred on the setting
selected on Delta; the grid edges are reported so a boundary winner is
visible. The selected and the Delta setting are then timed (one warmup, three
repetitions) on training and held-out sources. Every run is digest-checked.
--settings graph=threads:delta[,...] skips the grid and times only the given
settings, on the sources of --roles.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import statistics
import subprocess

from check_onenode_digest import check

DELTA_SELECTED = {'mesh26-z': (128, 4096), 'road-usa-z': (64, 32768)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root', type=Path)
    ap.add_argument('--graphs', default='mesh26-z,road-usa-z')
    ap.add_argument('--threads', default='32,64,96,112,128')
    ap.add_argument('--reps', type=int, default=3)
    ap.add_argument('--settings', default='', help='graph=threads:delta,... (skips tuning)')
    ap.add_argument('--roles', default='tune,test')
    args = ap.parse_args()
    job = os.environ['SLURM_JOB_ID']
    out = args.root / 'logs' / f'GAP-anvil-{job}'
    out.mkdir(parents=True, exist_ok=False)
    binary = args.root / 'bin' / 'gap_sssp'
    sha = hashlib.sha256(binary.read_bytes()).hexdigest()
    env = dict(os.environ, OMP_PROC_BIND='close', OMP_PLACES='cores')
    rng = random.Random(int(job))
    stream = (out / 'runs.jsonl').open('w', buffering=1)

    def run(graph, source, threads, delta, phase, rep, role):
        ref = args.root / 'graphs' / f'{graph}.reference.txt'
        log = out / f'{graph}-{phase}-t{threads}-d{delta}-{source}-r{rep}.out'
        command = ['srun', '-N', '1', '-n', '1', '-c', str(threads), '--cpu-bind=cores',
                   '--kill-on-bad-exit=1', str(binary), str(args.root / 'graphs' / f'{graph}.wsg'),
                   str(source), str(delta)]
        with log.open('w') as f:
            subprocess.run(command, env=dict(env, OMP_NUM_THREADS=str(threads)), stdout=f,
                           stderr=subprocess.STDOUT, check=True, timeout=180)
        check(ref, source, log)
        seconds = float(re.search(r'BENCH source=\d+ solve_seconds=([\d.eE+-]+)', log.read_text())[1])
        row = dict(graph=graph, source=source, role=role, phase=phase, rep=rep, threads=threads,
                   delta=delta, seconds=seconds, binary_sha256=sha, valid=True,
                   hosts=os.environ['SLURM_JOB_NODELIST'])
        stream.write(json.dumps(row) + '\n')
        return seconds

    summary = {}
    for graph in args.graphs.split(','):
        ref = args.root / 'graphs' / f'{graph}.reference.txt'
        rows = [r.split() for r in ref.read_text().splitlines() if r[:1].isdigit()]
        tune = [r[0] for r in rows if r[1] == 'tune']
        test = [r[0] for r in rows if r[1] == 'test']
        t0, d0 = DELTA_SELECTED[graph]
        fixed = [tuple(map(int, v.split('=')[1].split(':'))) for v in args.settings.split(',')
                 if v.split('=')[0] == graph]
        if fixed:
            timed = {}
            for t, d in fixed:
                label = f'fixed-t{t}-d{d}'
                run(graph, (test + tune)[0], t, d, 'warmup', -1, 'warmup')
                for role, sources in [('tune', tune), ('test', test)]:
                    if role not in args.roles.split(','):
                        continue
                    for s in sources:
                        values = [run(graph, s, t, d, label, rep, role) for rep in range(args.reps)]
                        timed.setdefault(label, {})[s] = dict(role=role, median=statistics.median(values))
            summary[graph] = dict(fixed=[dict(threads=t, delta=d) for t, d in fixed], timed=timed)
            continue
        deltas = [d0 // 4, d0 // 2, d0, d0 * 2, d0 * 4]
        threads = sorted(set(map(int, args.threads.split(','))) | {t0})
        grid = [(t, d) for t in threads for d in deltas]
        run(graph, tune[0], t0, d0, 'warmup', -1, 'tune')
        samples = {c: [] for c in grid}
        order = [(c, s) for c in grid for s in tune]
        rng.shuffle(order)
        for (t, d), s in order:
            samples[(t, d)].append(run(graph, s, t, d, 'tune', 0, 'tune'))
        score = {c: statistics.median(v) for c, v in samples.items()}
        best = min(score, key=score.get)
        edge = dict(threads=best[0] in (min(threads), max(threads)), delta=best[1] in (min(deltas), max(deltas)))
        timed = {}
        for label, (t, d) in [('selected', best), ('delta_setting', (t0, d0))]:
            if label == 'delta_setting' and (t, d) == best:
                continue
            for role, sources in [('tune', tune), ('test', test)]:
                for s in sources:
                    values = []
                    for rep in range(-1, args.reps):
                        v = run(graph, s, t, d, label, rep, role)
                        if rep >= 0:
                            values.append(v)
                    timed.setdefault(label, {})[s] = dict(role=role, median=statistics.median(values))
        summary[graph] = dict(selected=dict(threads=best[0], delta=best[1]), boundary=edge,
                              grid_scores={f't{t}-d{d}': v for (t, d), v in score.items()},
                              timed=timed)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({g: {label: {k: v['median'] for k, v in t.items()} for label, t in s['timed'].items()}
                      for g, s in summary.items()}, indent=2))


if __name__ == '__main__':
    main()
