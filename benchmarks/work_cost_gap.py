#!/usr/bin/env python3
"""R0 GAPBS reference: frozen tuning, interleaved production/counter builds."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import re
from check_onenode_digest import check
from work_cost_report import counters
import machine


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('graphs', nargs='?', default='mesh26-z,road-usa-z')
    parser.add_argument('--reps', type=int, default=2)
    # Training-selected GAPBS settings, graph:threads:delta. The default is
    # Delta's frozen choice; another machine must re-select with onenode_gap_tune.py.
    parser.add_argument('--settings', default='mesh26-z:128:4096,road-usa-z:64:32768')
    args = parser.parse_args()
    if args.reps < 1:
        parser.error('--reps must be positive')
    settings = [(g, int(t), int(d)) for g, t, d in (x.split(':') for x in args.settings.split(','))]
    if any(t > machine.NODE_CPUS for _, t, _ in settings):
        parser.error(f'{machine.MACHINE} has {machine.NODE_CPUS} usable cores; re-select GAPBS '
                     'settings with onenode_gap_tune.py and pass them with --settings')
    root = args.root
    graphs = set(args.graphs.split(','))
    if graphs - {g for g, _, _ in settings}:
        parser.error('unknown reference graph')
    out = root / 'logs' / f'GAP-R0-{os.environ["SLURM_JOB_ID"]}'
    out.mkdir(exist_ok=False)
    env = dict(os.environ, SLURM_MPI_TYPE='cray_shasta', OMP_PROC_BIND='close', OMP_PLACES='cores')
    with (out / 'runs.jsonl').open('w', buffering=1) as stream:
        for graph, threads, delta in settings:
            if graph not in graphs:
                continue
            ref = root / 'graphs' / f'{graph}.reference.txt'
            sources = [r for l in ref.read_text().splitlines() for r in [l.split()] if len(r) > 1 and r[1] == 'tune']
            for index, source in enumerate(sources):
                for rep in range(-1, args.reps):
                    labels = ['production', 'diagnostic'] if rep % 2 else ['diagnostic', 'production']
                    for label in labels:
                        counted = root / 'build/gap_r0/gap_work_cost'  # Delta layout; bin/ elsewhere
                        binary = root / ('bin/gap_sssp' if label == 'production' else
                                         (counted if counted.exists() else 'bin/gap_work_cost'))
                        log = out / f'{graph}-{label}-s{index}-r{rep}.out'
                        command = ['srun', '-N', '1', '-n', '1', '-c', str(threads), '--cpu-bind=cores',
                                   '--kill-on-bad-exit=1', str(binary), str(root/'graphs'/f'{graph}.wsg'), source[0], str(delta)]
                        with log.open('w') as f:
                            subprocess.run(command, env=dict(env, OMP_NUM_THREADS=str(threads)),
                                           stdout=f, stderr=subprocess.STDOUT, check=True, timeout=180)
                        check(ref, source[0], log)
                        text = log.read_text()
                        row = dict(graph=graph, source=int(source[0]), role='tune', rep=rep, variant=label,
                                   seconds=float(re.search(r'BENCH source=\d+ solve_seconds=([\d.eE+-]+)', text)[1]),
                                   binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                                   threads=threads, delta=delta, valid=True, hosts=os.environ['SLURM_JOB_NODELIST'],
                                   reachable_vertices=int(source[4]), reachable_arcs=int(source[7]))
                        if label == 'diagnostic':
                            row['counters'] = counters(text)
                        stream.write(json.dumps(row) + '\n')
    (out/'complete').write_text('All raw times, digests and counter identities passed.\n')


if __name__ == '__main__':
    main()
