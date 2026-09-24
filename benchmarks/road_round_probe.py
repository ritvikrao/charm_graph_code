#!/usr/bin/env python3
"""Bounded road round-cost attribution on a Slurm allocation.

Times use production binaries. Profile/cost/trace builds are separate, and
their perturbation is reported rather than silently used as performance data.
Only training sources are used to select an intervention. Every launch must
confirm the old scheduler; every road solve must match its independent digest.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

import machine
from check_onenode_digest import check
from work_cost_report import check_work_accounting, production_attempts


def file_hash(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def profile_summary(text, pes):
    records = {}
    for line in text.splitlines():
        if not line.startswith('ROUND_PROFILE '):
            continue
        fields = dict(item.split('=', 1) for item in line.split()[1:])
        pe = int(fields.pop('pe'))
        if pe in records:
            raise ValueError('duplicate round profile for PE %d' % pe)
        records[pe] = {name: [float(x) for x in value.split(',')]
                       for name, value in fields.items()}
    if set(records) != set(range(pes)):
        raise ValueError('missing round profiles')
    result = {}
    for name in records[0]:
        active = [row[name] for row in records.values() if row[name][0]]
        if active:
            result[name] = dict(
                pes=len(active), calls=sum(row[0] for row in active),
                pe_seconds=sum(row[1] for row in active),
                mean_us=1e6 * sum(row[1] for row in active) / sum(row[0] for row in active),
                max_pe_mean_us=max(1e6 * row[1] / row[0] for row in active),
                max_call_us=1e6 * max(row[2] for row in active))
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign', type=Path)
    ap.add_argument('--rpn', type=int, default=16)
    ap.add_argument('--reps', type=int, default=3)
    ap.add_argument('--trace', action='store_true')
    args = ap.parse_args()
    if args.reps < 1:
        ap.error('--reps must be positive')
    root = args.campaign.resolve()
    app = Path(__file__).resolve().parents[1]
    nodes = int(os.environ['SLURM_NNODES'])
    workers = machine.acic_workers(args.rpn)
    out = root / 'logs' / ('road-rounds-' + os.environ['SLURM_JOB_ID'])
    out.mkdir(exist_ok=False)
    ref = root / 'graphs/road-usa-z.reference.txt'
    metadata = (root/'graphs/road-usa-z.meta').read_text()
    vertices = int(re.search(r'vertices=(\d+)', metadata)[1])
    edges = int(re.search(r'arcs=(\d+)', metadata)[1])
    sources = [r.split()[0] for r in ref.read_text().splitlines()
               if r[:1].isdigit() and r.split()[1] == 'tune'][:2]
    if len(sources) != 2:
        raise ValueError('two training sources required')
    binaries = ['acic_latest', 'acic_quiet_rounds', 'acic_round_profile_v2',
                'acic_cost', 'round_trip']
    if args.trace:
        binaries.append('acic_round_trace')
    manifest = dict(nodes=nodes, rpn=args.rpn, workers=workers, sources=sources,
                    reps=args.reps, hosts=os.environ['SLURM_JOB_NODELIST'],
                    graph_sha256=file_hash(root/'graphs/road-usa-z.wsg'),
                    reference_sha256=file_hash(ref),
                    harness_sha256=file_hash(Path(__file__)),
                    vertices=vertices, stored_directed_edges=edges,
                    binaries={b: hashlib.sha256((root/'bin'/b).read_bytes()).hexdigest()
                              for b in binaries},
                    runtime=json.loads((root/'build/runtime-manifest.json').read_text()))
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    records = []

    def launch(binary, label, flags, rpn=args.rpn, extra_env=None):
        command = ['srun', '-N', str(nodes), '-n', str(nodes*rpn),
                   '--ntasks-per-node', str(rpn), '-c', str(machine.cpus_per_rank(rpn)),
                   *machine.acic_srun_extra(nodes), '--cpu-bind=none', '--unbuffered',
                   '--kill-on-bad-exit=1', '--time=00:04:00', 'bash',
                   str(app/'benchmarks/launch_acic.sh'), str(rpn),
                   str(machine.acic_workers(rpn)), str(root/'bin'/binary),
                   *flags, '+old-scheduler']
        log = out/(label + '.out')
        (out/(label + '.command.json')).write_text(json.dumps(command) + '\n')
        with log.open('w') as stream:
            subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                           env={**os.environ, **(extra_env or {})}, check=True, timeout=300)
        text = log.read_text()
        if 'Using the original scheduler (+old-scheduler)' not in text:
            raise ValueError('%s: old scheduler not confirmed' % log)
        return text

    # Real payload is 256 histogram bins + 11 control fields = 267 longs.
    # The smaller payload diagnoses serialization/reduction-volume sensitivity.
    for rpn in [8, 16]:
        for payload in [11, 267]:
            for rep in range(3):
                label = 'empty-%dx%d-p%d-r%d' % (
                    rpn, machine.acic_workers_per_rank(rpn), payload, rep)
                text = launch('round_trip', label, [str(payload), '1000', '100'], rpn)
                match = re.search(r'^ROUND_TRIP PASS (.*)$', text, re.M)
                if not match:
                    raise ValueError(label + ': missing round-trip validation')
                records.append(dict(kind='empty', label=label, rpn=rpn,
                                    result=dict(x.split('=') for x in match[1].split())))

    flags = ['0', str(root/'graphs/road-usa-z.wsg'), '1', sources[0],
             '4', '0.999', '0.005', '--sources', ','.join(sources),
             '--result-digest', '--timeout', '120', '--process-share', 'auto',
             '--reader-tile', 'auto', '--slack-control', 'off',
             '--process-queue', 'nearest', '--process-queue-batch', '8',
             '--heap-slice', '8', '--bucket-width', '131072', '--hub-hints', 'auto']

    def solve(binary, label, extra_flags=(), cost=False, profile=False, one_source=False):
        run_sources = sources[:1] if one_source else sources
        run_flags = list(flags)
        run_flags[run_flags.index('--sources') + 1] = ','.join(run_sources)
        text = launch(binary, label, run_flags + list(extra_flags))
        chunks = re.split(r'^SOURCE_RUN index=\d+ source=(\d+)\n', text, flags=re.M)
        if chunks[1::2] != run_sources:
            raise ValueError(label + ': missing or reordered sources')
        for source, chunk in zip(run_sources, chunks[2::2]):
            log = out/(label + '-s' + source + '.out')
            log.write_text(chunk)
            check(ref, source, log)
            seconds = float(re.search(r'^Compute time: ([\d.eE+-]+)', chunk, re.M)[1])
            rounds = int(re.search(r'^Number of reductions: (\d+)', chunk, re.M)[1])
            row = dict(kind='road', label=label, binary=binary, source=source,
                       seconds=seconds, rounds=rounds, us_per_round=1e6*seconds/rounds)
            row['edge_attempts'] = production_attempts(chunk, vertices)
            row['attempts_per_edge'] = row['edge_attempts'] / edges
            if profile:
                row['profile'] = profile_summary(chunk, nodes*workers)
            if cost:
                row['work'] = check_work_accounting(chunk, vertices)
            records.append(row)
        (out/'records.json').write_text(json.dumps(records, indent=2) + '\n')

    arms = [('base', 'acic_latest'), ('quiet', 'acic_quiet_rounds'),
            ('control', 'acic_latest')]
    for rep in range(-1, args.reps):
        for name, binary in arms[rep % 3:] + arms[:rep % 3]:
            solve(binary, '%s-r%d' % (name, rep))
    solve('acic_round_profile_v2', 'profile', profile=True)
    solve('acic_cost', 'cost', cost=True)
    if args.trace:
        trace_dir = out/'trace'
        trace_dir.mkdir()
        solve('acic_round_trace', 'trace', ['+traceoff', '+traceroot', str(trace_dir)],
              one_source=True)
        # One source: the trace window must not include between-source resets.
        subprocess.run([sys.executable, str(app/'benchmarks/projections_report.py'),
                        str(trace_dir/'acic_round_trace'), '--pes-per-process',
                        str(workers//args.rpn), '--processes-per-node', str(args.rpn),
                        '--jobs', '8', '--json', str(out/'trace-summary.json')],
                       stdout=(out/'trace-summary.txt').open('w'), check=True)
    summary = {}
    for source in sources:
        times = {name: statistics.median(row['seconds'] for row in records
                 if row['kind'] == 'road' and row['source'] == source
                 and re.fullmatch(name + r'-r\d+', row['label']))
                 for name, _ in arms}
        summary[source] = dict(times, quiet_speedup=times['base']/times['quiet'],
                               control_speedup=times['base']/times['control'])
    (out/'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (out/'complete').write_text('All digests, old-scheduler assertions and work audits passed.\n')
    print(json.dumps(summary, indent=2), flush=True)
    print('ROAD ROUND PROBE COMPLETE', out, flush=True)


if __name__ == '__main__':
    main()
