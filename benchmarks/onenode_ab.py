#!/usr/bin/env python3
"""Interleaved, fail-closed paired comparisons for each one-node lever.

Run inside a Slurm allocation. JSON variants specify binary, optional graph,
and flags. Reference row index pairs the same physical source after relabeling.
Two copies of the baseline estimate the allocation's timing floor. Run in two
allocations; this script records evidence and never silently adopts a default.
A variant may set its own nodes, rpn (processes per node) and workers (per
node), so layouts interleave within one allocation; the defaults use them all.
ACIC_SRUN_MPI selects the srun PMI plugin (cray_shasta on Delta, pmi2 on Anvil).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
from check_onenode_digest import check
import machine


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign', type=Path)
    ap.add_argument('graph')
    ap.add_argument('variants', type=Path)
    ap.add_argument('--reps', type=int, default=3)
    ap.add_argument('--workers', type=int, default=None,
                    help='ACIC workers per node (default: machine.py, 120 on Delta, 56 on Frontier)')
    ap.add_argument('--rpn', type=int, default=8)
    ap.add_argument('--sources', type=int, default=4)
    ap.add_argument('--source-role', choices=['tune', 'test'], default='test')
    ap.add_argument('--batch', action='store_true', help='all variants must support --sources')
    args = ap.parse_args()
    if args.workers is None:
        args.workers = machine.acic_workers(args.rpn)
    if args.reps < 1 or args.sources < 1 or args.rpn < 1 or args.workers < 1:
        ap.error('reps, sources, rpn and workers must be positive')
    nodes = int(os.environ['SLURM_NNODES'])
    out = args.campaign / 'logs' / f'AB-{args.graph}-{nodes}n-{os.environ["SLURM_JOB_ID"]}'
    out.mkdir(parents=True, exist_ok=False)
    variants = json.loads(args.variants.read_text())
    if len({v['label'] for v in variants}) != len(variants):
        raise ValueError('variant labels must be unique')
    for v in variants:
        if not re.fullmatch(r'[A-Za-z0-9_-]+', v['label']):
            raise ValueError('unsafe variant label')
        binary = args.campaign / 'bin' / v['binary']
        v['sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        v.setdefault('nodes', nodes)
        v.setdefault('rpn', args.rpn)
        v.setdefault('workers', args.workers)
        if not 1 <= v['nodes'] <= nodes or v['rpn'] < 1 or v['workers'] % v['rpn']:
            raise ValueError(f"{v['label']}: invalid layout")
    (out / 'manifest.json').write_text(json.dumps(dict(variants=variants, nodes=nodes,
        workers=args.workers, rpn=args.rpn, sources=args.sources, reps=args.reps,
        source_role=args.source_role, batch=args.batch,
        hosts=os.environ.get('SLURM_JOB_NODELIST')), indent=2))
    app = Path(__file__).resolve().parents[1]
    references = {}
    for v in variants:
        graph = v.get('graph', args.graph)
        path = args.campaign / 'graphs' / f'{graph}.reference.txt'
        rows = [r.split() for r in path.read_text().splitlines() if r[:1].isdigit()]
        references[graph] = (path, [r for r in rows if r[1] == args.source_role][:args.sources])
    # Roles and graph-invariant distance aggregates prove physical source pairing.
    base_rows = next(iter(references.values()))[1]
    if len(base_rows) != args.sources:
        raise ValueError(f'not enough {args.source_role} sources')
    for path, rows in references.values():
        if [r[4:7] for r in rows] != [r[4:7] for r in base_rows]:
            raise ValueError(f'{path}: sources are not paired with the baseline')
    env = dict(os.environ, SLURM_MPI_TYPE=os.environ.get('ACIC_SRUN_MPI', 'cray_shasta'),
               FI_CXI_RX_MATCH_MODE='hybrid')
    # LCI's bootstrap publishes one Cray PMI KVS entry per rank pair, so the
    # store must hold ranks^2 (512 ranks at 64 nodes x 8 is 262K; 100000
    # aborted every rank of job 5541371). Twice that, never below 100000.
    def kvs_entries(ranks):
        return str(max(100000, 2 * ranks * ranks, int(os.environ.get('PMI_MAX_KVS_ENTRIES', '0') or 0)))
    env.pop('LCT_PMI_BACKEND', None)
    records = []
    with (out / 'runs.jsonl').open('w', buffering=1) as stream:
        groups = [list(range(args.sources))] if args.batch else [[i] for i in range(args.sources)]
        for group in groups:
            for rep in range(-1, args.reps):  # warm every arm on every source
                rotated = variants[rep % len(variants):] + variants[:rep % len(variants)]
                for variant in rotated:
                    graph = variant.get('graph', args.graph)
                    refpath, refs = references[graph]
                    sources = [refs[i][0] for i in group]
                    log = out / f'{variant["label"]}-g{group[0]}-r{rep}.launch.out'
                    vn, rpn = variant['nodes'], variant['rpn']
                    command = ['srun', '-N', str(vn), '-n', str(vn * rpn),
                        '--ntasks-per-node', str(rpn), '-c', str(machine.cpus_per_rank(rpn)),
                        *machine.acic_srun_extra(vn), '--cpu-bind=none', '--unbuffered', '--kill-on-bad-exit=1', '--time=00:06:00',
                        'bash', str(app / 'benchmarks/launch_acic.sh'), str(rpn), str(variant['workers']),
                        str(args.campaign / 'bin' / variant['binary']), '0',
                        str(args.campaign / 'graphs' / f'{graph}.wsg'), '1', sources[0], '4', '0.999', '0.005',
                        '--result-digest', '--timeout', '300'] + variant.get('flags', [])
                    if args.batch:
                        command += ['--sources', ','.join(sources)]
                    with log.open('w') as output:
                        # A variant's "env" overrides the launch environment (network probes).
                        subprocess.run(command, env={**env, 'PMI_MAX_KVS_ENTRIES': kvs_entries(vn * rpn),
                                                     **variant.get('env', {})},
                                       stdout=output, stderr=subprocess.STDOUT,
                                       check=True, timeout=420)
                    if args.batch:
                        chunks = re.split(r'^SOURCE_RUN index=\d+ source=(\d+)\n', log.read_text(), flags=re.M)
                        if chunks[1::2] != sources:
                            raise ValueError(f'{log}: missing or reordered source results')
                        outputs = chunks[2::2]
                    else:
                        outputs = [log.read_text()]
                    for source_index, source, output in zip(group, sources, outputs):
                        result_log = out / f'{variant["label"]}-s{source_index}-r{rep}.out'
                        result_log.write_text(output)
                        check(refpath, source, result_log)
                        seconds = float(re.search(r'^Compute time: ([\d.eE+-]+)', output, re.M)[1])
                        row = dict(variant=variant['label'], graph=graph, source=source,
                                   source_index=source_index, rep=rep, seconds=seconds, valid=True,
                                   nodes=vn, rpn=rpn, workers=variant['workers'])
                        stream.write(json.dumps(row) + '\n')
                        if rep >= 0:
                            records.append(row)
    summary = {}
    for v in variants:
        summary[v['label']] = {str(i): statistics.median(r['seconds'] for r in records
            if r['variant'] == v['label'] and r['source_index'] == i) for i in range(args.sources)}
    (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
