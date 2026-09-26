#!/usr/bin/env python3
"""Paired, independently checked SSSP comparisons inside a Slurm allocation."""
import argparse
from collections import defaultdict
import csv
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import signal
import statistics
import struct
import subprocess
import time

from outcomes import classify
import machine

KEYS = ('h1', 'h2', 'reachable', 'distance_sum')
VARIANTS = {
    'current': [],
    'control': [],
    'old-fixed': ['--flush-policy', 'fixed', '--flush-interval', '5',
                  '--bucket-policy', 'fixed', '--idle-flush', 'off'],
    'open': [],  # Both admission percentiles set below.
    'fixed-1': ['--flush-policy', 'fixed', '--flush-interval', '1',
                '--bucket-policy', 'fixed', '--idle-flush', 'off'],
    'fixed-5': ['--flush-policy', 'fixed', '--flush-interval', '5',
                '--bucket-policy', 'fixed', '--idle-flush', 'off'],
}

# The `weight` width rule is a per-case setting, and the case is the mesh.
#
# Jobs 22063138, 22069453, 22069454 and 22069455 ran it at one, two, eight and
# sixteen nodes, all at eight processes of fifteen, 1080 timed runs, all valid.
# Paired speedups against the shipped `logv` rule with the clamp frozen, and
# the count of paired runs the rule won out of forty:
#
#   mesh20        1.20x  1.47x  1.56x  1.93x   40/40
#   mesh22        1.24x  1.39x  1.53x  1.67x   40/40
#   youtube       1.24x  1.08x  1.31x  1.25x   34/40
#   rmat20        1.15x  1.13x  1.06x  1.59x   34/40
#   rmat20-s2     1.25x  0.99x  1.35x  1.46x   32/40
#   road-ny       1.05x  1.09x  1.16x  1.18x   28/40
#   uniform20-s2  1.05x  0.74x  1.05x  1.22x   23/40
#   uniform20     1.10x  0.70x  0.93x  1.38x   22/40
#   rmat22        1.06x  0.86x  0.99x  1.42x   19/40
#
# Only mesh20 and mesh22 win every allocation on every source, and only they
# grow monotonically with node count. They are the map. The next three never
# regress but are weaker and do not clear their own control floors everywhere,
# and the last three change sign between allocations, so none of the six is
# established.
#
# It is emphatically not a default: at two nodes, four graphs regress, one of
# them by 1.42x on 2 of 12. And it is not the map this file had first. 7.6d
# measured road-ny at 2.73x and named it alone, on a table taken at one process
# of 120 workers; at a deployable layout road-ny is the weakest consistent win
# here and mesh20 and mesh22 -- excluded then as noise at 1.01x and 1.05x --
# are the whole result. The 7.6d table was a property of the process layout.
#
# Applied only to configs that ask for it (`per_graph_width`), never to an arm
# that already names a width or a rule, so no width A/B can be contaminated.
# The flag lands in `command` on every row and `bucket_width` parses back what
# the run actually bucketed with. `--per-graph-width off` disables the map.
PER_GRAPH_WIDTH_RULE = {'mesh20': 'weight', 'mesh22': 'weight'}


# Every loss of progress the step 7.5 campaign recorded, with the allocation
# and the policy that produced it. Each is replayed on two binaries: `acic`,
# which is the one that produced the failure, and `acic-progress`, the repaired
# one. A replay in which `acic` does not fail has reproduced nothing, and
# cannot be read as evidence that the repair works.
PROGRESS_CASES = [
    dict(case='rmat22-1x120-fixed-w3', job=22033411, nodes=1, workers=120,
         graph='rmat22', sources=[3882232, 2631403], rpn=1, name='tuned-fixed',
         flags=['--flush-policy', 'fixed', '--flush-interval', '1',
                '--bucket-policy', 'fixed', '--idle-flush', 'off',
                '--bucket-width', '3']),
    dict(case='rmat22-8x15-fixed-w3', job=22033887, nodes=1, workers=120,
         graph='rmat22', sources=[3882232], rpn=8, name='tuned-fixed',
         flags=['--flush-policy', 'fixed', '--flush-interval', '1',
                '--bucket-policy', 'fixed', '--idle-flush', 'off',
                '--bucket-width', '3']),
    dict(case='mesh20-2n-current', job=22032689, nodes=2, workers=16,
         graph='mesh20', sources=[736504], rpn=1, name='current', flags=[]),
]


class Campaign:
    def __init__(self, args):
        self.args = args
        self.root = Path(args.campaign)
        self.nodes = int(os.environ['SLURM_NNODES'])
        self.job = os.environ['SLURM_JOB_ID']
        self.tag = f'{args.mode}-{self.nodes}n-{args.workers}w-{self.job}'
        self.records = self.root / 'logs' / (self.tag + '.jsonl')
        self.raw = self.root / 'logs' / (self.tag + '.log.gz')
        # 1000 KVS entries was enough for Delta's one-rank-per-node pilot; the
        # 7.6f1 layouts reach 16 ranks per node on 16 nodes.
        self.env = dict(os.environ, PMI_MAX_KVS_ENTRIES='100000',
                        FI_CXI_RX_MATCH_MODE='hybrid', NO_AFFINITY='1',
                        OMP_PROC_BIND='close', OMP_PLACES='cores')
        self.count = 0
        self.binary_hashes = {}
        for path in (self.root/'bin').iterdir():
            if path.name in ['acic', 'acic_quiet', 'acic_progress', 'acic_shm', 'acic_width', 'acic_comm', 'acic_ipdps', 'acic_ipdps_wide', 'acic_ipdps2', 'acic_ipdps2_wide', 'riken_sssp',
                             'riken_sssp_mpit', 'mpi_share.so', 'gap_sssp', 'gluon_sssp', 'gluon_sssp64', 'wasp_sssp']:
                digest = hashlib.sha256()
                with path.open('rb') as f:
                    for chunk in iter(lambda: f.read(1048576), b''):
                        digest.update(chunk)
                self.binary_hashes[str(path)] = digest.hexdigest()

    def run(self, graph, source, config, expected, phase, rep=0, extra=None):
        engine = config['engine']
        # A layout that cannot split --workers evenly (7.6f1's 16 x 7) states
        # its own per-node total; the record keeps the allocation's budget.
        workers = config.get('workers', self.args.workers)
        per_graph_rule = None
        binary = self.root / 'bin' / {'acic': 'acic', 'acic-quiet': 'acic_quiet', 'acic-progress': 'acic_progress', 'acic-shm': 'acic_shm', 'acic-width': 'acic_width', 'acic-comm': 'acic_comm', 'acic-ipdps': self.args.ablation_binary, 'acic-ipdps-wide': self.args.ablation_binary + '_wide', 'acic-ipdps-prev': 'acic_ipdps', 'riken': 'riken_sssp', 'riken-mpit': 'riken_sssp_mpit', 'gap': 'gap_sssp', 'wasp': 'wasp_sssp', 'gluon': getattr(self.args, 'gluon_binary', 'gluon_sssp')}[engine]
        path = self.root / 'graphs' / (graph + '.wsg')
        env = dict(self.env)
        if 'presolve_seconds' in config:
            env['PRESOL_SECONDS'] = str(config['presolve_seconds'])
        # LCI reads its attributes from the environment, so a transport or
        # packet-pool variant is a config field rather than a command-line one.
        env.update({k: str(v) for k, v in config.get('env', {}).items()})
        if engine.startswith('acic'):
            pp = ['1.0', '1.0'] if config['name'] == 'open' else ['0.999', '0.005']
            # No +commap below: Reconverse has no communication thread
            # (reconverse/src/cpuaffinity.cpp: "also no commap, we have no
            # commthreads"), so the flag the recorded campaign passed was never
            # parsed. The core past the worker region is still left free for
            # the OS -- the pemap is unchanged -- so these runs are exactly
            # comparable with the recorded ones.
            flags = list(VARIANTS.get(config['name'], config.get('flags', []))) + list(extra or [])
            # A config that already states a width or a rule keeps it: an arm
            # of the width A/B must mean what its name says on every graph.
            if (config.get('per_graph_width') and self.args.per_graph_width == 'on'
                    and not any(f.startswith('--bucket-width') for f in flags)):
                rule = PER_GRAPH_WIDTH_RULE.get(graph)
                if rule:
                    flags += ['--bucket-width-rule', rule]
                    per_graph_rule = rule
            args = [str(binary), '0', str(path), '1', str(source), '4', *pp,
                    '--result-digest', '--timeout', str(self.args.timeout),
                    *flags, '+ppn', str(workers), '+pemap', machine.pemap(0, 1, workers),
                    '+lci_ndevices', '4']
            launch = ['srun', '-N', str(self.nodes), '-n', str(self.nodes),
                      '--ntasks-per-node=1', *machine.acic_srun_extra(self.nodes), '--cpu-bind=none']
            if config.get('rpn', 1) != 1:
                ranks = config['rpn']
                # Replace the trailing runtime flags with per-process maps.
                args = ['bash', str(Path(__file__).with_name('launch_acic.sh')),
                        str(ranks), str(workers), *args[:-6]]
                launch = ['srun', '-N', str(self.nodes), '-n', str(self.nodes*ranks),
                          '--ntasks-per-node', str(ranks), '-c', str(machine.cpus_per_rank(ranks)),
                          *machine.acic_srun_extra(self.nodes), '--cpu-bind=none']
        elif engine == 'gluon':
            # `threads`/`cpus`, when a config states them, are per rank.
            ranks = config.get('rpn', 1)
            threads = config.get('threads', workers)
            launch = ['srun', '-N', str(self.nodes), '-n', str(self.nodes*ranks),
                      '--ntasks-per-node', str(ranks), '-c', str(config.get('cpus', threads+1)),
                      '--cpu-bind=cores']
            args = [str(binary), str(path.with_suffix('.gr')), '-symmetricGraph',
                    '-exec='+config['model'], '-startNode='+str(source), '-t='+str(threads),
                    '-runs=1', '-maxIterations=2147483647', '-delta='+str(config['delta']),
                    '-partition='+config['partition']]
        else:
            ranks = config.get('rpn', 1)
            threads = config.get('threads', workers // ranks)
            env['OMP_NUM_THREADS'] = str(threads)
            launch = ['srun', '-N', str(self.nodes), '-n', str(self.nodes*ranks),
                      '--ntasks-per-node', str(ranks), '-c', str(config.get('cpus', threads)),
                      '--cpu-bind=cores']
            args = [str(binary), str(path), str(source), str(config['delta'])]
            if engine == 'riken-mpit':
                # Preloaded into the solver only: srun itself is not an MPI program.
                args = ['env', f'LD_PRELOAD={self.root/"bin"/"mpi_share.so"}', *args]
            if engine.startswith('riken'):
                args += [str(config['denominator']), str(config.get('presolve', 0))]
            elif self.nodes != 1:
                raise ValueError('GAPBS and Wasp are single-node baselines')
        cmd = [*launch, '--unbuffered', '--kill-on-bad-exit=1', *args]
        self.count += 1
        record = dict(job=self.job, nodes=self.nodes, workers=self.args.workers,
                      graph=graph, source=source, phase=phase, rep=rep,
                      selection_job=self.args.selection_job,
                      config=config, index=self.count,
                      binary_sha256=self.binary_hashes[str(binary)],
                      hosts=os.environ.get('SLURM_JOB_NODELIST'), command=cmd,
                      per_graph_width_rule=per_graph_rule)
        begin = time.monotonic()
        with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, env=env, start_new_session=True) as process:
            try:
                output, _ = process.communicate(timeout=config.get('launch_timeout_seconds', self.args.timeout+90))
                record['returncode'] = process.returncode
            except subprocess.TimeoutExpired:
                # Signal only this launcher's process group. srun tears down
                # its own step; never kill another user's or allocation's jobs.
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    output, _ = process.communicate(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    output, _ = process.communicate()
                record['returncode'] = -999
        record['launch_wall_seconds'] = time.monotonic()-begin
        pattern = r'^VERIFY parallel digest (.*)$' if engine.startswith('acic') else r'^BENCH (.*)$'
        match = re.search(pattern, output, re.M)
        digest = {k: int(v) for k, v in re.findall(r'(h1|h2|reachable|distance_sum)=(\d+)', match[1])} if match else {}
        record['digest'] = digest
        record['expected'] = {k: int(expected[k]) for k in KEYS}
        record['valid'] = record['returncode'] == 0 and digest == record['expected']
        # RIKEN aims only at Graph500 accuracy (validate.hpp: relative 1e-5 per
        # edge), and on the OSM roads it leaves a few distances slightly long.
        # --riken-tolerance accepts such a run as timed but inexact: every
        # vertex reached, and the distance sum high by at most that fraction.
        if engine.startswith('riken') and record['returncode'] == 0 and digest:
            expected_sum = record['expected']['distance_sum']
            record['exact'] = digest == record['expected']
            record['relative_distance_error'] = (digest['distance_sum']-expected_sum) / max(1, expected_sum)
            if (not record['exact'] and self.args.riken_tolerance > 0 and
                    digest['reachable'] == record['expected']['reachable'] and
                    0 <= record['relative_distance_error'] <= self.args.riken_tolerance):
                record['valid'] = True
        t = re.search(r'^Compute time: ([\d.eE+-]+)', output, re.M) if engine.startswith('acic') else re.search(r'^BENCH .*?\bsolve_seconds=([\d.eE+-]+)', output, re.M)
        if t:
            record['seconds'] = float(t[1])
            record['valid'] = record['valid'] and math.isfinite(record['seconds']) and record['seconds'] > 0
        else:
            record['valid'] = False
        # 7.6h: what kind of failure, not only whether. A run that stopped is a
        # different result from one that answered wrongly or crashed, and a
        # cell's hang count is reported beside its speedup (outcomes.py). The
        # solver prints TIMEOUT from fast_exit() and PROGRESS_STALL while it is
        # stuck; the harness kill is -999. A rescued run finishes and is valid,
        # but it would have hung, so the rescue count is kept on every row.
        if engine.startswith('acic'):
            record['progress_stall_lines'] = len(re.findall(r'^PROGRESS_STALL \d+ main', output, re.M))
            record['hung'] = (record['returncode'] == -999 or
                              bool(re.search(r'^TIMEOUT', output, re.M)) or
                              (not record['valid'] and record['progress_stall_lines'] > 0))
            for field, regex in {'stall_rescues': r'^Stall rescues: (\d+)',
                                 'skew_top_arrivals': r'^Skewed top-bucket arrivals: (\d+)'}.items():
                value = re.search(regex, output, re.M)
                if value:
                    record[field] = int(value[1])
        else:
            record['hung'] = record['returncode'] == -999
        record['outcome'] = classify(record)
        for field, regex in {
            # -1 from any of these means the mode did not measure that phase,
            # not that it took no time. setup + compute + stats == total.
            'read_seconds': r'^Read time: ([\d.eE+-]+)',
            'setup_seconds': r'^Setup time: ([\d.eE+-]+)',
            'index_seconds': r'^Index time: ([\d.eE+-]+)',
            'stats_seconds': r'^Stats time: ([\d.eE+-]+)',
            # Compute time is not here because it is already `seconds`, parsed
            # above: it, and not the harness wall clock, is what every A/B in
            # this file compares. launch_wall_seconds is the wall clock.
            'total_seconds': r'^Total time: ([\d.eE+-]+)',
            # What the histogram actually bucketed with. Until 7.6d no run said,
            # which is how a width derived from |V| bucketed distances for a
            # whole campaign without anyone reading the number.
            'bucket_width': r'^Bucket width: ([\d.eE+-]+)',
            'heaviest_edge': r'^Bucket width: [\d.eE+-]+, heaviest edge: (\d+)',
            'bucket_scale': r'^Bucket scale: (\d+)',
            'coarsenings': r'^Bucket scale: \d+ \((\d+) coarsenings\)',
            'reductions': r'^Number of reductions: (\d+)',
            'updates_noted': r'^Updates noted: (\d+)',
            'distance_changes': r'^Distance changes: (\d+)',
            'rejected': r'^Rejected updates: (\d+)',
            'tram_bytes': r'^TRAM messages: \d+, bytes sent: (\d+)',
            'tram_messages': r'^TRAM messages: (\d+)',
            'construction_seconds': r'construction_seconds=([\d.eE+-]+)',
            'presolve_seconds': r'presolve_seconds=([\d.eE+-]+)',
            # 7.6o communication shares: acic-comm, riken-mpit, and every
            # Gluon run (its own sync timer, max over hosts, in ms).
            'comm_compute_share': r'^COMM_SHARE .*\bcompute_share=([\d.eE+-]+)',
            'comm_send_share': r'^COMM_SHARE .*\bsend_share=([\d.eE+-]+)',
            'comm_other_share': r'^COMM_SHARE .*\bother_share=([\d.eE+-]+)',
            'mpi_share': r'^MPI_SHARE .*\bshare=([\d.eE+-]+)',
            'gluon_sync_ms': r'^STAT, 0, Gluon, Sync_SSSP_0, HMAX, (\d+)',
            'gluon_timer_ms': r'^STAT, 0, SSSP, Timer_0, HMAX, (\d+)',
        }.items():
            value = re.search(regex, output, re.M)
            if value:
                record[field] = float(value[1])
        with gzip.open(self.raw, 'at') as f:
            f.write('\nRUN ' + json.dumps(record) + '\n' + output)
        with self.records.open('a') as f:
            f.write(json.dumps(record) + '\n')
        print(f'{self.tag} #{self.count} {phase} {graph} src={source} {config} '
              f'valid={record["valid"]} outcome={record["outcome"]} '
              f'seconds={record.get("seconds")}', flush=True)
        if not record['valid']:
            print(output[-6000:], flush=True)
        return record

    def references(self, graph):
        text = (self.root/'graphs'/(graph+'.reference.txt')).read_text()
        return list(csv.DictReader(text[text.index('source\t'):].splitlines(), delimiter='\t'))

    def skip_failed(self, graph, row, config, phase, rep):
        """Account for an unrun slot after its variant failed validation."""
        self.count += 1
        record = dict(job=self.job, nodes=self.nodes, workers=self.args.workers,
                      selection_job=self.args.selection_job, graph=graph,
                      source=int(row['source']), config=config, phase=phase,
                      rep=rep, index=self.count, valid=False, skipped=True,
                      reason='not run after this variant failed validation',
                      expected={k:int(row[k]) for k in KEYS})
        with self.records.open('a') as f:
            f.write(json.dumps(record)+'\n')
        with gzip.open(self.raw, 'at') as f:
            f.write('\nSKIPPED '+json.dumps(record)+'\n')
        print(f'{self.tag} #{self.count} SKIPPED {graph} {config["name"]} source={row["source"]}', flush=True)

    def configs(self, graph):
        meta = dict(re.findall(r'(\w+)=(\d+)', (self.root/'graphs'/(graph+'.meta')).read_text()))
        denominator = int(meta['riken_denominator'])
        deltas = sorted({max(1, denominator // k) for k in [64, 16, 4, 1]})
        training_max = max(int(r['max_distance']) for r in self.references(graph) if r['role'] == 'tune')
        widths = sorted({0, max(1, math.ceil(training_max/1024)), denominator})
        result = [dict(engine='acic', name=f'fixed-{i}-w{width}',
                       flags=VARIANTS[f'fixed-{i}'] + (['--bucket-width', str(width)] if width else []))
                  for i in [1, 5] for width in widths]
        for rpn in map(int, self.args.ranks_per_node.split(',')):
            for delta in deltas:
                result.append(dict(engine='riken', name='riken', rpn=rpn,
                                   delta=delta, denominator=denominator, presolve=0))
        if self.nodes == 1:
            result += [dict(engine='gap', name='gap', delta=d) for d in deltas]
        return result

    def benchmark(self):
        rng = random.Random(20260913 + self.nodes + self.args.workers)
        graphs = self.args.graphs.split(',')
        rng.shuffle(graphs)
        for graph in graphs:
            references = self.references(graph)
            training = [x for x in references if x['role'] == 'tune']
            held_out = [x for x in references if x['role'] == 'test'][:self.args.sources]
            candidates = self.configs(graph)
            # One discarded warmup per implementation; every measured query
            # still starts a fresh process and includes solver initialization.
            for engine in ['acic', 'riken'] + (['gap'] if self.nodes == 1 else []):
                config = next(c for c in candidates if c['engine'] == engine)
                self.run(graph, int(training[0]['source']), config, training[0], 'warmup')
            samples = {json.dumps(c, sort_keys=True): [] for c in candidates}
            for row in training:
                order = candidates[:]
                rng.shuffle(order)
                for config in order:
                    r = self.run(graph, int(row['source']), config, row, 'tune')
                    samples[json.dumps(config, sort_keys=True)].append(r)
            selected = []
            for engine in ['acic', 'riken'] + (['gap'] if self.nodes == 1 else []):
                choices = [c for c in candidates if c['engine'] == engine and
                           all(r['valid'] for r in samples[json.dumps(c, sort_keys=True)])]
                if not choices:
                    raise RuntimeError(f'No valid {engine} configuration for {graph}')
                config = min(choices, key=lambda c: statistics.geometric_mean(
                    r['seconds'] for r in samples[json.dumps(c, sort_keys=True)]))
                selected.append(dict(config, name='tuned-fixed' if engine == 'acic' else engine,
                                     **({'flags': config['flags']} if engine == 'acic' else {})))
            # `current` and `control` are the shipped adaptive configuration
            # and its resolution floor -- the pair that stands for "ACIC as we
            # would run it" in the comparison tables -- so they, and not the
            # historical `old-fixed`/`open` arms, carry the per-graph width
            # rule. The tuned-fixed arm already selects its own width on the
            # training sources, and an explicit --bucket-width makes the rule
            # inert anyway.
            selected += [dict(engine='acic', name=n, per_graph_width=True)
                         for n in ['current', 'control']]
            selected += [dict(engine='acic', name=n) for n in ['old-fixed', 'open']]
            (self.root/'logs'/(self.tag+'-'+graph+'-selected.json')).write_text(json.dumps(selected, indent=2)+'\n')
            for rep in range(self.args.reps):
                rows = held_out[:]
                rng.shuffle(rows)
                for row in rows:
                    order = selected[:]
                    rng.shuffle(order)
                    for config in order:
                        r = self.run(graph, int(row['source']), config, row, 'test', rep)
                        if not r['valid']:
                            raise RuntimeError('Failed correctness or execution gate; see raw log')

    def smoke(self):
        for graph in self.args.graphs.split(','):
            row = self.references(graph)[0]
            configs = [dict(engine='acic', name='current')]
            configs += [c for c in self.configs(graph) if c['engine'] != 'acic'
                        and c['delta'] == c.get('denominator', 1024)//4]
            for config in configs:
                if not self.run(graph, int(row['source']), config, row, 'smoke')['valid']:
                    raise RuntimeError('Smoke check failed')

    def presolve(self):
        # A bounded preprocessing sensitivity check; do not call this the
        # upstream uncapped 4000-round benchmark configuration.
        for graph in self.args.graphs.split(','):
            references = self.references(graph)
            config = next(c for c in self.configs(graph) if c['engine'] == 'riken'
                          and c['rpn'] == 4 and c['delta'] == c['denominator']//4)
            for row in references[:2]:
                for seconds in [0, 30]:
                    candidate = dict(config, name='riken-pre30' if seconds else 'riken-no-pre',
                                     presolve=4000 if seconds else 0, presolve_seconds=max(1, seconds))
                    if not self.run(graph, int(row['source']), candidate, row, 'presolve-probe')['valid']:
                        raise RuntimeError('Preprocessing probe failed verification')

    def confirm(self):
        if not self.args.selection_job:
            raise ValueError('confirm requires --selection-job with frozen pilot choices')
        rng = random.Random(20260913 + int(self.job))
        for graph in self.args.graphs.split(','):
            path = self.root/'logs'/f'benchmark-{self.nodes}n-{self.args.workers}w-{self.args.selection_job}-{graph}-selected.json'
            frozen = json.loads(path.read_text())
            old_riken = next(c for c in frozen if c['engine'] == 'riken')
            fixed = next(c for c in frozen if c['name'] == 'tuned-fixed')
            references = self.references(graph)
            candidates = [c for c in self.configs(graph) if c['engine'] == 'riken'] + [old_riken]
            samples = {json.dumps(c, sort_keys=True): [] for c in candidates}
            for row in references[:2]:
                order = candidates[:]
                rng.shuffle(order)
                for c in order:
                    r = self.run(graph, int(row['source']), c, row, 'confirm-tune')
                    samples[json.dumps(c, sort_keys=True)].append(r)
            valid = [c for c in candidates if all(r['valid'] for r in samples[json.dumps(c, sort_keys=True)])]
            best = min(valid, key=lambda c: statistics.geometric_mean(r['seconds'] for r in samples[json.dumps(c, sort_keys=True)]))
            configs = [dict(engine='acic', name='current'), dict(engine='acic', name='control'),
                       fixed, old_riken, dict(best, name='riken-retuned')]
            for rep in range(self.args.reps):
                rows = references[2:2+self.args.sources]
                rng.shuffle(rows)
                for row in rows:
                    order = configs[:]
                    rng.shuffle(order)
                    for c in order:
                        if not self.run(graph, int(row['source']), c, row, 'confirm-test', rep)['valid']:
                            raise RuntimeError('Confirmation failed verification')

    def gluon(self):
        rng = random.Random(20260913 + self.nodes)
        for graph in self.args.graphs.split(','):
            refs = self.references(graph)
            denominator = next(c['denominator'] for c in self.configs(graph) if c['engine'] == 'riken')
            candidates = [dict(engine='gluon', name='gluon-'+model.lower(), model=model,
                               partition=partition, delta=delta)
                          for model in ['Async', 'Sync']
                          for partition in (['oec'] if self.nodes == 1 else ['oec', 'cvc'])
                          for delta in [0, denominator//16, denominator]]
            samples = {json.dumps(c, sort_keys=True): [] for c in candidates}
            for model in ['Async', 'Sync']:
                c = next(c for c in candidates if c['model'] == model)
                self.run(graph, int(refs[0]['source']), c, refs[0], 'gluon-warmup')
            for row in refs[:2]:
                order = candidates[:]
                rng.shuffle(order)
                for c in order:
                    r = self.run(graph, int(row['source']), c, row, 'gluon-tune')
                    samples[json.dumps(c, sort_keys=True)].append(r)
            selected = []
            for model in ['Async', 'Sync']:
                valid = [c for c in candidates if c['model'] == model and all(r['valid'] for r in samples[json.dumps(c, sort_keys=True)])]
                if not valid:
                    raise RuntimeError('No valid Gluon configuration for '+graph+' '+model)
                selected.append(min(valid, key=lambda c: statistics.geometric_mean(r['seconds'] for r in samples[json.dumps(c, sort_keys=True)])))
            selected += [dict(engine='acic', name='current'), dict(engine='acic', name='control')]
            (self.root/'logs'/(self.tag+'-'+graph+'-selected.json')).write_text(json.dumps(selected, indent=2)+'\n')
            for rep in range(self.args.reps):
                rows = refs[2:2+self.args.sources]
                rng.shuffle(rows)
                for row in rows:
                    order = selected[:]
                    rng.shuffle(order)
                    for c in order:
                        if not self.run(graph, int(row['source']), c, row, 'gluon-test', rep)['valid']:
                            raise RuntimeError('Gluon comparison failed verification')

    def numa(self):
        if self.args.workers != 120:
            raise ValueError('The NUMA geometry probe is specified for 120 workers/node')
        rng = random.Random(20260913 + int(self.job))
        configs = []
        for ranks in [1, 4, 8]:
            for policy in ['current', 'fixed']:
                configs.append(dict(engine='acic', name=f'{policy}-{ranks}x{120//ranks}', rpn=ranks,
                                    flags=[] if policy == 'current' else VARIANTS['fixed-1']+['--bucket-width','1024']))
        for graph in self.args.graphs.split(','):
            refs = self.references(graph)
            for c in configs:
                if not self.run(graph, int(refs[0]['source']), c, refs[0], 'numa-warmup')['valid']:
                    raise RuntimeError('NUMA layout failed verification')
            for rep in range(self.args.reps):
                rows = refs[2:2+self.args.sources]
                rng.shuffle(rows)
                for row in rows:
                    order = configs[:]
                    rng.shuffle(order)
                    for c in order:
                        if not self.run(graph, int(row['source']), c, row, 'numa-test', rep)['valid']:
                            raise RuntimeError('NUMA layout failed verification')

    def finish(self):
        """Complete unattempted slots after a failed job; never replace failures."""
        if not self.args.selection_job:
            raise ValueError('finish requires --selection-job')
        tag = f'benchmark-{self.nodes}n-{self.args.workers}w-{self.args.selection_job}'
        old = [json.loads(s) for s in (self.root/'logs'/(tag+'.jsonl')).read_text().splitlines()]
        rng = random.Random(20260913 + int(self.job))
        for graph in self.args.graphs.split(','):
            refs = self.references(graph)[2:2+self.args.sources]
            selected = json.loads((self.root/'logs'/(tag+'-'+graph+'-selected.json')).read_text())
            seen = {(r['source'], r['rep'], r['config']['name']) for r in old
                    if r['graph'] == graph and r['phase'] == 'test'}
            for rep in range(self.args.reps):
                rows = refs[:]
                rng.shuffle(rows)
                for row in rows:
                    order = selected[:]
                    rng.shuffle(order)
                    for c in order:
                        if (int(row['source']), rep, c['name']) not in seen:
                            self.run(graph, int(row['source']), c, row, 'completion-test', rep)
            # Diagnostic reruns are separate, including failures, and cannot
            # rescue the original censored performance cell.
            failed = [r for r in old if r['graph'] == graph and r['phase'] == 'test' and not r['valid']]
            for original in failed:
                row = next(r for r in refs if int(r['source']) == original['source'])
                other = 'current' if original['config']['name'] == 'tuned-fixed' else 'tuned-fixed'
                for rep in range(5):
                    order = [original['config'], next(c for c in selected if c['name'] == other)]
                    rng.shuffle(order)
                    for c in order:
                        self.run(graph, original['source'], c, row, 'failure-replay', rep)

    def layout_confirm(self):
        """Pair the selected deployment geometry with frozen CPU baselines."""
        if self.nodes != 1 or self.args.workers != 120 or not self.args.selection_job:
            raise ValueError('layout_confirm requires one node, 120 workers, and --selection-job')
        rng = random.Random(20260913 + int(self.job))
        for graph in self.args.graphs.split(','):
            path = self.root/'logs'/f'benchmark-1n-120w-{self.args.selection_job}-{graph}-selected.json'
            frozen = json.loads(path.read_text())
            configs = [dict(engine='acic', name=n, rpn=8) for n in ['current', 'control']]
            configs += [dict(c, rpn=8) if c['engine']=='acic' else c
                        for c in frozen if c['name'] in ['tuned-fixed', 'riken', 'gap']]
            refs = self.references(graph)
            for c in configs:
                if not self.run(graph, int(refs[0]['source']), c, refs[0], 'layout-warmup')['valid']:
                    raise RuntimeError('Layout confirmation failed verification')
            for rep in range(self.args.reps):
                rows = refs[2:2+self.args.sources]
                rng.shuffle(rows)
                for row in rows:
                    order = configs[:]
                    rng.shuffle(order)
                    for c in order:
                        if not self.run(graph, int(row['source']), c, row, 'layout-test', rep)['valid']:
                            raise RuntimeError('Layout confirmation failed verification')

    def quiet(self):
        if self.nodes != 1 or self.args.workers != 16 or not self.args.selection_job:
            raise ValueError('quiet requires one node, 16 workers, and --selection-job')
        rng = random.Random(20260913 + int(self.job))
        for graph in self.args.graphs.split(','):
            frozen = json.loads((self.root/'logs'/f'benchmark-1n-16w-{self.args.selection_job}-{graph}-selected.json').read_text())
            configs = [dict(engine='acic', name='current'), dict(engine='acic-quiet', name='quiet')]
            configs += [c for c in frozen if c['engine'] in ['riken', 'gap']]
            refs = self.references(graph)
            for c in configs:
                if not self.run(graph, int(refs[0]['source']), c, refs[0], 'quiet-warmup')['valid']:
                    raise RuntimeError('Quiet comparison failed verification')
            for rep in range(self.args.reps):
                rows = refs[2:2+self.args.sources]
                rng.shuffle(rows)
                for row in rows:
                    order = configs[:]
                    rng.shuffle(order)
                    for c in order:
                        if not self.run(graph, int(row['source']), c, row, 'quiet-test', rep)['valid']:
                            raise RuntimeError('Quiet comparison failed verification')

    def finish_layout(self):
        if self.nodes != 1 or self.args.workers != 120 or not self.args.selection_job:
            raise ValueError('finish_layout requires one node, 120 workers, and --selection-job')
        tag = f'layout_confirm-1n-120w-{self.args.selection_job}'
        old = [json.loads(s) for s in (self.root/'logs'/(tag+'.jsonl')).read_text().splitlines()]
        primary_job = old[0]['selection_job']
        rng = random.Random(20260913 + int(self.job))
        for graph in self.args.graphs.split(','):
            frozen = json.loads((self.root/'logs'/f'benchmark-1n-120w-{primary_job}-{graph}-selected.json').read_text())
            configs = [dict(engine='acic', name=n, rpn=8) for n in ['current','control']]
            configs += [dict(c,rpn=8) if c['engine']=='acic' else c for c in frozen
                        if c['name'] in ['tuned-fixed','riken','gap']]
            seen = {(r['source'],r['rep'],r['config']['name']) for r in old if r['graph']==graph and r['phase']=='layout-test'}
            failed = {r['config']['name'] for r in old if r['graph']==graph and not r['valid']}
            refs = self.references(graph)
            for c in configs:
                if c['name'] not in failed:
                    r = self.run(graph,int(refs[0]['source']),c,refs[0],'layout-completion-warmup')
                    if not r['valid']:
                        failed.add(c['name'])
            for rep in range(self.args.reps):
                rows = refs[2:2+self.args.sources]
                rng.shuffle(rows)
                for row in rows:
                    order = configs[:]
                    rng.shuffle(order)
                    for c in order:
                        if (int(row['source']),rep,c['name']) in seen:
                            continue
                        if c['name'] in failed:
                            self.skip_failed(graph,row,c,'layout-completion',rep)
                        elif not self.run(graph,int(row['source']),c,row,'layout-completion',rep)['valid']:
                            failed.add(c['name'])

    def controller(self):
        """Item 2 of the step 7.5 next work: vary one controller rule at a time.

        Process geometry is frozen at 16 workers per node and one process per
        node, so the only thing moving is the rule under test. Each variant
        differs from current defaults by exactly one flag, and `control` is
        current defaults run a second time -- the difference between current
        and control is what this allocation can resolve, and no variant closer
        than that is a result.

        `tuned-fixed` comes from the frozen 7.5 selection for this graph and
        node count, so the strong fixed policy is the one that was actually
        tuned rather than one invented here.

        One untimed diagnostic query per variant writes the round series, which
        carries `coarsen_reason` for every round: why that round could or could
        not merge buckets. Time and delivered work are both recorded, because
        fewer rounds is not less work and neither one is less time.
        """
        rng = random.Random(20260913 + int(self.job))
        # Every variant here runs the repaired binary: the knobs below do not
        # exist in the one the 7.5 campaign measured, and a controller result
        # that straddled a progress repair would not mean anything anyway.
        engine = 'acic-progress'
        for graph in self.args.graphs.split(','):
            refs = self.references(graph)
            meta = dict(re.findall(r'(\w+)=(\d+)',
                                   (self.root/'graphs'/(graph+'.meta')).read_text()))
            denominator = int(meta['riken_denominator'])
            training_max = max(int(r['max_distance']) for r in refs
                               if r['role'] == 'tune')
            # The same two width candidates the fixed-policy search used, so a
            # width result here can be compared with that search rather than
            # standing on its own.
            widths = sorted({max(1, math.ceil(training_max/1024)), denominator})
            configs = [dict(engine=engine, name='current'),
                       dict(engine=engine, name='control')]
            if self.args.selection_job:
                tag = (f'benchmark-{self.nodes}n-{self.args.workers}w-'
                       f'{self.args.selection_job}-{graph}-selected.json')
                path = self.root/'logs'/tag
                if path.exists():
                    frozen = json.loads(path.read_text())
                    configs += [dict(c, engine=engine) for c in frozen
                                if c['name'] == 'tuned-fixed']
            for width in widths:
                configs.append(dict(engine=engine, name=f'width-{width}',
                                    flags=['--bucket-width', str(width)]))
            # The two-tier branch abandons both percentiles for 0.9999. Its
            # eligibility is N * 100 with N the PE count, so the same graph
            # enters it at a proportionally larger population on more PEs.
            # 1600 is what that rule gives on one 16-worker node, held fixed.
            configs += [
                dict(engine=engine, name='two-tier-absolute-1600',
                     flags=['--two-tier-absolute', '1600']),
                dict(engine=engine, name='two-tier-never',
                     flags=['--two-tier-absolute', '0']),
                dict(engine=engine, name='clamp-strict',
                     flags=['--coarsen-clamped', 'strict']),
                # No clamp-allow here. Merging over a non-empty clamp bucket
                # strands live counts at 2047 / k, and a 10,000-vertex mesh
                # already shows what that does: the window pins to the stranded
                # bucket, the run never converges, and 45 seconds of it wrote a
                # 574 MB log. There is nothing to time and the log would fill
                # scratch; see design/step76-progress.md.
                dict(engine=engine, name='window-follow',
                     flags=['--window-follow', 'on']),
            ]
            # Untimed, one query, on a tuning source: the round series says why
            # each round could or could not merge, which no timing can.
            for c in configs:
                prefix = (self.root/'logs'/f'controller-{self.tag}-{graph}-'
                          f'{c["name"]}')
                self.run(graph, int(refs[0]['source']), c, refs[0],
                         'controller-diag', extra=['--diag', str(prefix)])
            for rep in range(self.args.reps):
                rows = refs[2:2+self.args.sources]
                rng.shuffle(rows)
                for row in rows:
                    order = configs[:]
                    rng.shuffle(order)
                    for c in order:
                        self.run(graph, int(row['source']), c, row,
                                 'controller-test', rep)

    def bimodal(self):
        """Wave 2 found mesh20 at two nodes splitting into two regimes.

        Sixteen identical runs under current defaults delivered either about
        7M updates or about 55-80M, with nothing in between, while every
        variant that pins the admission rule -- a fixed width, an absolute
        two-tier limit, the tuned fixed policy -- stayed inside 1.5x. The
        split is not machine weather: interleaved in the same minutes, the
        pinned variants never flipped. The bad regime runs about 950 rounds
        where the good one runs about 2400, so it is coarse admission rather
        than extra iteration.

        No diagnostic query caught it, because the campaign ran one per
        variant and that one landed in the good regime. This mode runs
        current defaults repeatedly, each with its own round series, so that
        a good and a bad trace differ only in which regime they fell into.
        """
        engine = 'acic-progress'
        for graph in self.args.graphs.split(','):
            refs = self.references(graph)
            row = refs[2]
            config = dict(engine=engine, name='current')
            for rep in range(self.args.reps):
                prefix = (self.root/'logs'/f'bimodal-{self.tag}-{graph}-'
                          f'rep{rep:02d}')
                self.run(graph, int(row['source']), config, row,
                         'bimodal', rep, extra=['--diag', str(prefix)])

    def deployment(self):
        """Item 3 of the 7.5 next work: separate the deployment variables.

        The item names four axes and asks for them apart rather than together,
        because the 7.5 NUMA probe moved process geometry, communication
        endpoints and the CkNumNodes()-dependent starvation threshold at once
        and so could not attribute what it measured. Each config here differs
        from the 8 x 15 candidate by one thing.

        Process count is the rpn sweep at fixed total occupancy: 120 workers
        per node throughout, so 1 x 120 through 8 x 15 change how many
        processes carry the same threads. Note this cannot fully isolate
        process count, because the starvation threshold reads CkNumNodes();
        the idle-flush arms below are what separate that, by holding rpn at 8
        and moving the policy instead.

        Transport is the one axis that needs a second binary. The linked tree
        reconverse-linux-x86_64-shmem is built with LCI_WITH_SHM 0 and the
        unused -shm tree beside it with LCI_WITH_SHM 1; see
        design/step76-deployment.md. bin/acic_shm is the same source built
        against the latter, so `transport-shm` differs from `layout-8x15` by
        the LCI shared-memory backend and nothing else.

        The packet-pool arms test whether the missing UCX registration cache
        is on the hot path at all. Registration only happens on the rendezvous
        path, which LCI takes above max_bcopy_size, derived from packet_size
        (8192 as built). Raising packet_size past the measured message sizes
        moves those graphs onto the eager path and out of registration
        entirely; npackets has to fall to match, or 64 KB x 65536 would be
        4 GB per process.
        """
        # PRECONDITION: bin/acic_progress and bin/acic_shm must be built from
        # the same source, differing only in which reconverse tree they link.
        # They are two binaries, so a transport result that straddled a source
        # change would be measuring the source change. Restage both together,
        # and not while a controller job that pins acic_progress is queued.
        engine = 'acic-progress'
        base = dict(engine=engine, name='layout-8x15', rpn=8)
        configs = [
            base,
            # Process count, at fixed total occupancy.
            dict(engine=engine, name='layout-1x120', rpn=1),
            dict(engine=engine, name='layout-2x60', rpn=2),
            dict(engine=engine, name='layout-4x30', rpn=4),
            # Starvation policy, with the geometry frozen at the candidate.
            dict(engine=engine, name='idle-flush-off', rpn=8,
                 flags=['--idle-flush', 'off']),
            dict(engine=engine, name='idle-flush-on', rpn=8,
                 flags=['--idle-flush', 'on']),
            # Transport endpoints: same source, LCI shared memory enabled.
            dict(engine='acic-shm', name='transport-shm', rpn=8),
            # Packet pool: footprint, and whether rendezvous costs anything.
            dict(engine=engine, name='packets-8192', rpn=8,
                 env={'LCI_ATTR_NPACKETS': 8192}),
            dict(engine=engine, name='packets-eager-64k', rpn=8,
                 env={'LCI_ATTR_PACKET_SIZE': 65536,
                      'LCI_ATTR_NPACKETS': 4096}),
        ]
        rng = random.Random(20260914 + int(self.job))
        for graph in self.args.graphs.split(','):
            refs = self.references(graph)
            for rep in range(self.args.reps):
                rows = refs[2:2+self.args.sources]
                rng.shuffle(rows)
                for row in rows:
                    order = configs[:]
                    rng.shuffle(order)
                    for c in order:
                        self.run(graph, int(row['source']), c, row,
                                 'deployment', rep)

    def width(self):
        """The 7.6d repair: bucket by the graph's heaviest edge, not by |V|.

        Two changes ship together and this separates them, because they are
        independent and only one of them is expected to move a clock.

        The width rule is the one with a measured prior: `weight` reproduces
        the `width-1024` arm of 7.6b on the eight graphs whose weights top out
        at 1000, and that arm won on mesh20 and mesh22 at every allocation from
        two to sixteen nodes and lost no race in 64 runs. What has no prior is
        the four graphs 7.6b never varied the width on -- rmat20, rmat20-s2,
        uniform20, uniform20-s2 -- which the rule calls safe already and where
        a width of 1000 against a distance range near 1,300 puts the entire
        graph in the first two buckets. If the rule costs anything anywhere, it
        is there, so they are in this campaign and they are the reason it runs
        over all nine graphs rather than the three that motivated it.

        The clamp freeze should be invisible here. It changes what a merge does
        and the width rule is chosen so that merging is not needed, so a
        difference between `weight` and `weight-unfrozen` would mean the width
        rule is not doing what it claims. `logv-frozen` is the other half of
        that cross: the freeze on the old width, where merging does happen.

        Every arm runs one binary, so no result here straddles a build.
        """
        rng = random.Random(20260914 + int(self.job))
        engine = 'acic-width'
        rpn = dict(rpn=self.args.acic_rpn)
        configs = [
            # The shipped behaviour, reproduced exactly on the new binary.
            dict(engine=engine, name='logv', **rpn,
                 flags=['--bucket-width-rule', 'logv', '--clamp-freeze', 'off']),
            dict(engine=engine, name='logv-frozen', **rpn,
                 flags=['--bucket-width-rule', 'logv', '--clamp-freeze', 'on']),
            dict(engine=engine, name='weight-unfrozen', **rpn,
                 flags=['--bucket-width-rule', 'weight', '--clamp-freeze', 'off']),
            dict(engine=engine, name='weight', **rpn,
                 flags=['--bucket-width-rule', 'weight', '--clamp-freeze', 'on']),
            # 7.6e. The three graphs the width rule wins on are not blocked on
            # coarsening -- no round on any of them was ever refused for the
            # clamp -- they are out of range, and spend most of a run with the
            # whole live population in the overflow slot. This arm keeps the
            # logv width and lets the clamp rise instead, which is the same
            # range the weight rule buys without the resolution the weight rule
            # spends to buy it. It should be indistinguishable from
            # `logv-frozen` on the five graphs already in range, where it
            # extends zero times.
            dict(engine=engine, name='logv-range', **rpn,
                 flags=['--bucket-width-rule', 'logv', '--clamp-freeze', 'on',
                        '--range-extend', 'on']),
            # The shipped default, unflagged, run twice under two names: the
            # difference between `control` and its explicit twin is what this
            # allocation can resolve, and no arm closer than that is a result.
            # In 22061958 the default was `weight`, so `control` twinned that
            # arm; 3977158 moved the default to logv + freeze, so it now
            # twins `logv-frozen`. Every other arm here states its own flags
            # precisely so that the default may move again without silently
            # renaming a result.
            dict(engine=engine, name='control', **rpn),
        ]
        if self.args.arms:
            keep = self.args.arms.split(',')
            configs = [c for c in configs if c['name'] in keep]
            if len(configs) != len(keep):
                raise ValueError(f'unknown arm in --arms {self.args.arms}')
        for graph in self.args.graphs.split(','):
            refs = self.references(graph)
            for c in configs:
                prefix = (self.root/'logs'/f'width-{self.tag}-{graph}-{c["name"]}')
                self.run(graph, int(refs[0]['source']), c, refs[0],
                         'width-diag', extra=['--diag', str(prefix)])
            for rep in range(self.args.reps):
                rows = refs[2:2+self.args.sources]
                rng.shuffle(rows)
                for row in rows:
                    order = configs[:]
                    rng.shuffle(order)
                    for c in order:
                        self.run(graph, int(row['source']), c, row, 'width', rep)

    def policy(self):
        """7.6f2: admission x delivery, against a global and a per-case fixed setting.

        The research claim is that feedback steering both work admission and
        delivery beats what a fixed configuration can do. That needs three
        comparisons, and a campaign that runs only one of them can be read as
        supporting the claim when it does not:

        * the 2 x 2 matrix, each axis fixed or adaptive, so a benefit can be
          assigned to admission, to delivery, or to the two together.
          Admission adaptive is `--bucket-policy adaptive` (the controller
          coarsens its buckets); fixed keeps the initial width. Delivery
          adaptive is the shipped flush policy plus the starvation-gated idle
          flush; fixed is a flush every round and no idle flush, which is what
          26 of the 30 step 7.5 tuned-fixed selections chose. Every matrix arm
          takes its width from the shipped rule and the per-graph map, so the
          matrix differs only in the two axes;
        * `global-fixed`, one fixed configuration chosen on the development
          graphs' tuning sources only, and then frozen for every graph -- the
          "good default" the adaptive method has to beat;
        * `tuned-fixed`, the best fixed configuration per graph, chosen on that
          graph's tuning sources -- the reference the adaptive method should
          stay near, and cannot be expected to beat.

        `local-delivery` (added after the first two waves) keeps adaptive
        admission and takes the delivery policy's actions ungated by the
        controller's global starvation signal: the interaction test.

        `control` is `adaptive` under another name: the resolution floor, per
        allocation (outcomes.py). Failures do not stop the campaign -- a hang
        rate is a result here, and report_arms.py puts it beside every ratio.
        Timed runs use the held-out test sources; the tuning sources only
        choose. Results on the development graphs are in-sample for
        `global-fixed` and are marked so by the report's graph list, not here.
        """
        rng = random.Random(20260915 + int(self.job))
        rpn = dict(rpn=self.args.acic_rpn)
        delivery_fixed = ['--flush-policy', 'fixed', '--flush-interval', '1', '--idle-flush', 'off']
        admission_fixed = ['--bucket-policy', 'fixed']
        matrix = [
            dict(engine='acic', name='adaptive', per_graph_width=True, **rpn),
            dict(engine='acic', name='control', per_graph_width=True, **rpn),
            dict(engine='acic', name='adapt-admission', per_graph_width=True, flags=delivery_fixed, **rpn),
            dict(engine='acic', name='adapt-delivery', per_graph_width=True, flags=admission_fixed, **rpn),
            dict(engine='acic', name='fixed-both', per_graph_width=True, flags=admission_fixed + delivery_fixed, **rpn),
            # The co-design test. The shipped delivery policy takes the same two
            # actions -- flush destinations with no full send since last round,
            # and flush on idle -- but only when the controller's global
            # histogram says the run is starved. This arm takes them on local
            # state alone, with admission unchanged. If it matches `adaptive`,
            # the shared signal is not what delivery gains from, and the claim
            # is two mechanisms rather than one co-designed feedback loop. The
            # first two waves could not ask this: the 2 x 2 above has no arm
            # with adaptive delivery that ignores the global signal.
            dict(engine='acic', name='local-delivery', per_graph_width=True,
                 flags=['--flush-policy', 'stale', '--idle-flush', 'on'], **rpn),
        ]
        graphs = self.args.graphs.split(',')
        dev = self.args.dev_graphs.split(',')
        # Global candidates must mean the same thing on every graph, so widths
        # are rules rather than numbers: road-ny's heaviest edge is 37x mesh's.
        global_candidates = [
            dict(engine='acic', name=f'global-{rule}-i{interval}', **rpn,
                 flags=['--flush-policy', 'fixed', '--flush-interval', str(interval),
                        '--bucket-policy', 'fixed', '--idle-flush', 'off',
                        '--bucket-width-rule', rule])
            for rule in ['logv', 'weight'] for interval in [1, 5]]

        def choose(candidates, samples, label):
            valid = [c for c in candidates if samples[c['name']] and
                     all(r['valid'] for r in samples[c['name']])]
            record = {c['name']: dict(valid=c in valid,
                                      seconds=[r.get('seconds') for r in samples[c['name']]])
                      for c in candidates}
            if not valid:
                raise RuntimeError(f'no valid candidate for {label}: {record}')
            best = min(valid, key=lambda c: statistics.geometric_mean(
                r['seconds'] for r in samples[c['name']]))
            return best, record

        # Global fixed: chosen once for this allocation, on development graphs.
        samples = {c['name']: [] for c in global_candidates}
        for graph in dev:
            for row in [x for x in self.references(graph) if x['role'] == 'tune']:
                order = global_candidates[:]
                rng.shuffle(order)
                for c in order:
                    samples[c['name']].append(self.run(graph, int(row['source']), c, row, 'policy-global-tune'))
        best, record = choose(global_candidates, samples, 'global-fixed')
        global_fixed = dict(best, name='global-fixed', chosen=best['name'])
        (self.root/'logs'/f'{self.tag}-global-selected.json').write_text(
            json.dumps(dict(dev_graphs=dev, selected=global_fixed, candidates=record), indent=2)+'\n')

        for graph in graphs:
            refs = self.references(graph)
            tune = [x for x in refs if x['role'] == 'tune']
            test = [x for x in refs if x['role'] == 'test'][:self.args.sources]
            # Per-case fixed: the step 7.5 search space, on this graph's tuning
            # sources, with the geometry this campaign runs at.
            candidates = [dict(c, **rpn) for c in self.configs(graph) if c['engine'] == 'acic']
            samples = {c['name']: [] for c in candidates}
            for row in tune:
                order = candidates[:]
                rng.shuffle(order)
                for c in order:
                    samples[c['name']].append(self.run(graph, int(row['source']), c, row, 'policy-tune'))
            best, record = choose(candidates, samples, f'tuned-fixed on {graph}')
            tuned = dict(best, name='tuned-fixed', chosen=best['name'])
            arms = matrix + [global_fixed, tuned]
            (self.root/'logs'/f'{self.tag}-{graph}-selected.json').write_text(
                json.dumps(dict(arms=arms, tuned_candidates=record), indent=2)+'\n')
            for rep in range(self.args.reps):
                rows = test[:]
                rng.shuffle(rows)
                for row in rows:
                    order = arms[:]
                    rng.shuffle(order)
                    for c in order:
                        self.run(graph, int(row['source']), c, row, 'policy', rep)

    def ablation(self):
        """IPDPS sprint: what each post-2024 mechanism buys, on one binary.

        design/ipdps27-sprint.md has the arm list and the reading rules. Every
        arm is the 8g default with one mechanism turned off, except:

        * `ws24`, the repaired 2024-style solver: flush every five rounds, no
          coarsening, no idle flush, a fixed 2048-item buffer, no send filter,
          no lazy relaxation and no delivery skipping -- with every correctness
          repair (progress rescue, overflow ordering) kept. `ws24-wide` is the
          same on the 16-byte wire build, so the wire format's share of the
          gain is separate from the mechanisms';
        * `global-fixed`, one configuration with every runtime-adaptive choice
          fixed, chosen once on --dev-graphs' tuning sources;
        * `tuned-fixed`, the same space plus explicit widths and buffer sizes,
          chosen per graph on its own tuning sources (a coordinate search:
          cadence x width at the regime's buffer size, then buffer size).

        A one-axis arm whose mechanism the regime rules leave off on a graph
        is the default under another name, so it is not run there. The
        process layout per graph is --acic-rpn-map (8g's choice at this node
        count); `control` is `current` again, the allocation's floor.
        """
        rng = random.Random(20260918 + int(self.job))
        rpn_map = dict(x.split(':') for x in self.args.acic_rpn_map.split(',')) if self.args.acic_rpn_map else {}

        def base(graph):
            r = int(rpn_map.get(graph, self.args.acic_rpn))
            return dict(engine='acic-ipdps', per_graph_width=True, rpn=r, workers=machine.acic_workers(r))

        def regime(graph):
            meta = dict(re.findall(r'(\w+)=(\d+)', (self.root/'graphs'/(graph+'.meta')).read_text()))
            degree = int(meta['arcs']) / int(meta['vertices'])
            # sssp_smp.cpp's initial_buffer_size(): 256 items per unit of
            # average degree, to a multiple of 256 in [512, 6144]. Lazy
            # relaxation, delivery skipping and the 30 us idle-flush interval
            # turn on at degree >= 8, the send filter at 2048 items or more.
            items = int(min(256 * degree, 6144))
            initial = min(6144, max(512, max(256, (items + 128) // 256 * 256)))
            return dict(degree=degree, scale_free=degree >= 8, buffer=initial,
                        filter=initial >= 2048, denominator=int(meta['riken_denominator']))

        ws24 = ['--flush-policy', 'fixed', '--flush-interval', '5', '--bucket-policy', 'fixed',
                '--idle-flush', 'off', '--bufsize', '2048', '--send-filter', 'off',
                '--lazy-heavy', 'off', '--skip-empty', 'off']
        # --fixed-idle on,off adds an ungated idle flush (with the regime's
        # interval) to the fixed search space; the first allocations searched
        # `off` only, which left tuned-fixed without the mechanism that the
        # 8-node high-diameter ablation found worth 2-3.6x (job 20826392).
        idle_options = self.args.fixed_idle.split(',')
        suffix = '' if idle_options == ['off'] else '-idle'

        def arms_for(graph):
            g = regime(graph)
            b = base(graph)
            arms = [dict(b, name='current'), dict(b, name='control'),
                    dict(b, name='ws24', flags=ws24),
                    dict(b, name='ws24-wide', engine='acic-ipdps-wide', flags=ws24),
                    dict(b, name='fixed-cadence', flags=['--flush-policy', 'fixed', '--flush-interval', '1']),
                    dict(b, name='no-idle-flush', flags=['--idle-flush', 'off']),
                    dict(b, name='no-coarsen', flags=['--bucket-policy', 'fixed']),
                    dict(b, name='no-idle-interval', flags=['--idle-flush-interval', '0']),
                    # A fixed 2048 would also switch the send filter on where
                    # the regime leaves it off; keep the filter as it was.
                    dict(b, name='buffer-2048', flags=['--bufsize', '2048']
                         + ([] if g['filter'] else ['--send-filter', 'off'])),
                    dict(b, name='buffer-no-feedback', flags=['--bufsize', str(g['buffer'])]),
                    # The controller's starvation gate on the idle flush and
                    # the stale-destination flush, removed: both actions taken
                    # on local state alone (7.6f2's co-design test).
                    dict(b, name='idle-ungated', flags=['--idle-flush', 'on']),
                    dict(b, name='local-delivery', flags=['--flush-policy', 'stale', '--idle-flush', 'on'])]
            if g['scale_free']:
                arms += [dict(b, name='no-lazy', flags=['--lazy-heavy', 'off', '--skip-empty', 'on']),
                         dict(b, name='no-skip-empty', flags=['--skip-empty', 'off'])]
            if g['filter']:
                arms += [dict(b, name='no-filter', flags=['--send-filter', 'off'])]
            if self.args.ablation_binary != 'acic_ipdps':
                # The first allocations' binary at its own defaults: how much
                # of a difference is the rebuild itself (09-19: rmat25 at 2
                # nodes read ~7% apart between two builds of the same solve).
                arms += [dict(b, name='prev-binary', engine='acic-ipdps-prev')]
            keep = self.args.arms.split(',') if self.args.arms else None
            return [a for a in arms if keep is None or a['name'] in keep]

        def fixed_candidates(graph, widths, buffers):
            b = base(graph)
            return [dict(b, name=f'fixed-i{i}-{w}-b{s}' + ('' if idle == 'off' else '-idle'), per_graph_width=False,
                         flags=['--flush-policy', 'fixed', '--flush-interval', str(i), '--bucket-policy', 'fixed',
                                '--idle-flush', idle]
                         + (['--bucket-width-rule', w] if w in ('logv', 'weight') else ['--bucket-width', w])
                         + ['--bufsize', str(s)])
                    for i in [1, 5] for w in widths for s in buffers for idle in idle_options]

        def choose(candidates, samples, label):
            valid = [c for c in candidates if samples[c['name']] and all(r['valid'] for r in samples[c['name']])]
            record = {c['name']: dict(valid=c in valid, seconds=[r.get('seconds') for r in samples[c['name']]])
                      for c in candidates}
            if not valid:
                raise RuntimeError(f'no valid candidate for {label}: {record}')
            return min(valid, key=lambda c: statistics.geometric_mean(r['seconds'] for r in samples[c['name']])), record

        def search(graph, candidates, phase, samples=None):
            samples = samples if samples is not None else {}
            for c in candidates:
                samples.setdefault(c['name'], [])
            for row in [x for x in self.references(graph) if x['role'] == 'tune']:
                order = candidates[:]
                rng.shuffle(order)
                for c in order:
                    samples[c['name']].append(self.run(graph, int(row['source']), c, row, phase))
            return samples

        # Global fixed: every rule-based width x cadence x two buffer sizes,
        # on the development graphs, each at its own layout.
        dev = self.args.dev_graphs.split(',')
        global_samples = {}
        names = None
        for graph in dev:
            cands = fixed_candidates(graph, ['logv', 'weight'], [1024, 2048])
            names = [c['name'] for c in cands]
            search(graph, cands, 'ablation-global-tune', global_samples)
        best, record = choose([dict(name=n) for n in names], global_samples, 'global-fixed')
        global_flags = next(c for c in fixed_candidates(dev[0], ['logv', 'weight'], [1024, 2048])
                            if c['name'] == best['name'])['flags']
        (self.root/'logs'/f'{self.tag}-global-selected.json').write_text(
            json.dumps(dict(dev_graphs=dev, selected=best['name'], flags=global_flags, candidates=record), indent=2)+'\n')

        for graph in self.args.graphs.split(','):
            g = regime(graph)
            refs = self.references(graph)
            test = [x for x in refs if x['role'] == 'test'][:self.args.sources]
            training_max = max(int(r['max_distance']) for r in refs if r['role'] == 'tune')
            widths = ['logv', 'weight', str(max(1, math.ceil(training_max/1024)))]
            warm = dict(base(graph), name='warmup')
            self.run(graph, int(refs[0]['source']), warm, refs[0], 'warmup')
            stage1 = fixed_candidates(graph, widths, [g['buffer']])
            samples = search(graph, stage1, 'ablation-tune')
            first, _ = choose(stage1, samples, f'tuned-fixed stage 1 on {graph}')
            i = first['flags'][first['flags'].index('--flush-interval')+1]
            w = first['name'].split('-')[2]
            idle = first['flags'][first['flags'].index('--idle-flush')+1]
            stage2 = [c for c in fixed_candidates(graph, [w], [512, 1024, 2048, 6144])
                      if c['name'].startswith(f'fixed-i{i}-') and c['name'] != first['name']
                      and c['flags'][c['flags'].index('--idle-flush')+1] == idle]
            samples = search(graph, stage2, 'ablation-tune', samples)
            tuned, record = choose(stage1 + stage2, samples, f'tuned-fixed on {graph}')
            b = base(graph)
            arms = arms_for(graph) + [
                dict(b, name='global-fixed' + suffix, per_graph_width=False, flags=global_flags, chosen=best['name']),
                dict(tuned, name='tuned-fixed' + suffix, chosen=tuned['name'])]
            (self.root/'logs'/f'{self.tag}-{graph}-selected.json').write_text(
                json.dumps(dict(regime=g, arms=arms, tuned_candidates=record), indent=2)+'\n')
            for rep in range(self.args.reps):
                rows = test[:]
                rng.shuffle(rows)
                for row in rows:
                    order = arms[:]
                    rng.shuffle(order)
                    for c in order:
                        self.run(graph, int(row['source']), c, row, 'ablation', rep)

    def external_candidates(self, graph):
        """7.6f1 search space: every system picks its own process layout.

        One rule sizes every layout, ACIC's included: a process gets its
        128/rpn-core stride and runs one fewer thread than that, so each
        process leaves a core to the OS (or, for Gluon, to its communication
        thread). That is the 8 x 15 layout ACIC has measured since step 7.5.
        ACIC layouts keep a process inside one 16-core NUMA domain; the
        baselines may span domains if that is what they prefer.

        Returns (layout candidates, parameter candidates) per arm. The layout
        candidates run the arm's default parameters; the parameter candidates
        are built for a chosen layout by `tune(layout)`.
        """
        meta = dict(re.findall(r'(\w+)=(\d+)', (self.root/'graphs'/(graph+'.meta')).read_text()))
        denominator = int(meta['riken_denominator'])
        deltas = sorted({max(1, denominator // k) for k in map(int, self.args.delta_divisors.split(','))})
        mid = max(1, denominator // 16)
        stride = machine.cpus_per_rank  # machine.py: 128/rpn on Delta, 56/rpn on Frontier
        layouts = lambda arg: [int(x) for x in arg.split(',')]
        arms = {}

        def acic_layout(rpn):
            return dict(rpn=rpn, workers=machine.acic_workers(rpn))
        arms['adaptive'] = ([dict(engine='acic', name=f'adaptive-r{r}', per_graph_width=True, **acic_layout(r))
                             for r in layouts(self.args.acic_layouts)], None)
        fixed = [c for c in self.configs(graph) if c['engine'] == 'acic']
        arms['tuned-fixed'] = (
            [dict(fixed[0], name=f'{fixed[0]["name"]}-r{r}', **acic_layout(r)) for r in layouts(self.args.acic_layouts)],
            lambda layout: [dict(c, name=f'{c["name"]}-r{layout["rpn"]}', rpn=layout['rpn'], workers=layout['workers'])
                            for c in fixed])
        # RIKEN's binary32 distances are exact only below 2^24 (riken_driver.cpp).
        if max(int(r['max_distance']) for r in self.references(graph)) < 16777216:
            def riken(rpn, delta):
                return dict(engine='riken', name=f'riken-r{rpn}-d{delta}', rpn=rpn, threads=machine.threads_per_rank(rpn),
                            cpus=stride(rpn), delta=delta, denominator=denominator, presolve=0)
            arms['riken'] = ([riken(r, mid) for r in layouts(self.args.riken_layouts)],
                             lambda layout: [riken(layout['rpn'], d) for d in deltas])
        partitions = ['oec'] if self.nodes == 1 else self.args.gluon_partitions.split(',')
        # Gluon's -delta raises the priority threshold by delta every round
        # (sssp_push.cpp), so a solve takes at least max_distance/delta rounds.
        # On meshes and roads d/16 and d mean thousands of rounds; the optional
        # multipliers add larger steps that still leave four or more rounds.
        max_distance = max(int(r['max_distance']) for r in self.references(graph))
        gluon_deltas = sorted({0, mid, denominator} |
                              {denominator * m for m in map(int, filter(None, self.args.gluon_delta_multipliers.split(',')))
                               if denominator * m <= max_distance // 4})
        # --gluon-deltas pins the grid: the largest graphs reuse the delta their
        # family's smaller members selected, since a full search would not fit.
        if self.args.gluon_deltas:
            gluon_deltas = sorted(int(d) for d in self.args.gluon_deltas.split(','))

        layout_delta = (gluon_deltas[0] if self.args.gluon_deltas
                        else max(1, denominator // self.args.gluon_layout_divisor))

        def gluon(model, rpn, partition, delta):
            return dict(engine='gluon', name=f'gluon-{model.lower()}-r{rpn}-{partition}-d{delta}', model=model,
                        rpn=rpn, threads=machine.gluon_threads_per_rank(rpn), cpus=stride(rpn), partition=partition, delta=delta)
        for model in ['Async', 'Sync']:
            arms['gluon-'+model.lower()] = (
                [gluon(model, r, p, layout_delta)
                 for r in layouts(self.args.gluon_layouts) for p in partitions],
                lambda layout, model=model: [gluon(model, layout['rpn'], layout['partition'], d) for d in gluon_deltas])
        if self.nodes == 1:
            def gap(threads, delta):
                return dict(engine='gap', name=f'gap-t{threads}-d{delta}', threads=threads, cpus=threads, delta=delta)
            arms['gap'] = ([gap(t, mid) for t in layouts(self.args.gap_threads)],
                           lambda layout: [gap(layout['threads'], d) for d in deltas])
        keep = self.args.external_arms.split(',')
        return {arm: v for arm, v in arms.items() if arm in keep}

    def external(self):
        """7.6f1: ACIC against RIKEN, Gluon and GAPBS, each at its own layout.

        Step 7.5 compared the systems at one geometry chosen for ACIC, and the
        geometry turned out to matter more than any policy (7.6c: 7-20x). Here
        each system tunes its own, on equal nodes, with the same two-stage
        search:

        1. layout: the arm's default parameters (ACIC adaptive or its first
           fixed candidate, mid delta for the baselines) at every candidate
           layout, on the first tuning source (layouts differ by integer
           factors, 7.6c, so one source separates them);
        2. parameters: at the chosen layout, the arm's parameter grid on both
           tuning sources (ACIC fixed: flush x width; RIKEN, GAPBS: four
           deltas; Gluon: three priorities, Async and Sync separately).

        This is a coordinate search, not an oracle over the joint space, and
        it gives every system the same budget shape. The chosen configurations
        then run on the held-out sources in random order with `control`, the
        chosen adaptive configuration under another name, as the floor.
        A system with no valid candidate is recorded and left out of the test
        phase: a baseline that cannot run a case is a result, not a crash.
        """
        rng = random.Random(20260916 + int(self.job))

        def geomean(runs):
            return statistics.geometric_mean(r['seconds'] for r in runs)

        for graph in self.args.graphs.split(','):
            refs = self.references(graph)
            tune = [x for x in refs if x['role'] == 'tune']
            test = [x for x in refs if x['role'] == 'test'][:self.args.sources]
            arms = self.external_candidates(graph)
            samples = defaultdict(list)
            if self.args.external_no_search:
                # Pinned: each arm's first layout candidate (its pinned
                # settings) goes straight to the held-out sources, with no
                # warmup or search. For graphs whose baseline solves take tens
                # of minutes (terrain-ae-z), where the settings come from a
                # separate pilot.
                selected = {arm: layouts[0] for arm, (layouts, _) in arms.items()}
                record = {arm: dict(layout=c['name'], reason='pinned (--external-no-search)')
                          for arm, c in selected.items()}
            else:
                # One discarded warmup per system, so a cold page cache is not
                # charged to whichever candidate happens to run first.
                for engine in sorted({c['engine'] for layouts, _ in arms.values() for c in layouts}):
                    c = next(c for layouts, _ in arms.values() for c in layouts if c['engine'] == engine)
                    self.run(graph, int(tune[0]['source']), c, tune[0], 'external-warmup')

                def measure(candidates, rows, stage):
                    # A candidate already run on a row (the chosen layout's default
                    # parameters) is not run on it again.
                    order = [(row, c) for row in rows for c in candidates
                             if int(row['source']) not in {r['source'] for r in samples[c['name']]}]
                    rng.shuffle(order)
                    for row, c in order:
                        samples[c['name']].append(self.run(graph, int(row['source']), c, row, 'external-'+stage))

                layout_candidates = [c for layouts, _ in arms.values() for c in layouts]
                measure(layout_candidates, tune[:1], 'layout')
                selected, record = {}, {}
                for arm, (layouts, grid) in arms.items():
                    valid = [c for c in layouts if all(r['valid'] for r in samples[c['name']])]
                    if not valid:
                        record[arm] = dict(selected=None, reason='no valid layout')
                        continue
                    layout = min(valid, key=lambda c: geomean(samples[c['name']]))
                    selected[arm] = layout
                    record[arm] = dict(layout=layout['name'])
                parameter_candidates = {arm: arms[arm][1](selected[arm]) for arm in selected if arms[arm][1]}
                measure([c for cs in parameter_candidates.values() for c in cs], tune, 'parameters')
                for arm, candidates in parameter_candidates.items():
                    valid = [c for c in candidates if all(r['valid'] for r in samples[c['name']])]
                    if valid:
                        selected[arm] = min(valid, key=lambda c: geomean(samples[c['name']]))
                    else:
                        record[arm] = dict(record[arm], reason='no valid parameters at the chosen layout')
                        del selected[arm]
            test_arms = [dict(c, name=arm, chosen=c['name']) for arm, c in selected.items()]
            if 'adaptive' in selected:
                test_arms.append(dict(selected['adaptive'], name='control', chosen=selected['adaptive']['name']))
            for arm, c in selected.items():
                record[arm] = dict(record[arm], selected=c['name'])
            record['candidates'] = {name: dict(valid=all(r['valid'] for r in runs),
                                               seconds=[r.get('seconds') for r in runs],
                                               outcomes=[r['outcome'] for r in runs])
                                    for name, runs in samples.items()}
            (self.root/'logs'/f'{self.tag}-{graph}-selected.json').write_text(
                json.dumps(dict(arms=test_arms, search=record), indent=2)+'\n')
            for rep in range(self.args.reps):
                rows = test[:]
                rng.shuffle(rows)
                for row in rows:
                    order = test_arms[:]
                    rng.shuffle(order)
                    for c in order:
                        self.run(graph, int(row['source']), c, row, 'external', rep)

    def comm_share(self):
        """7.6o: where each system's solve time goes, at this node count.

        Runs after `--mode external` in the same allocation and reads its
        choices (`external-...-<job>-<graph>-selected.json`). ACIC's and
        RIKEN's chosen configurations run on the first test sources twice
        each, once plain and once on the timing build (`acic_comm`,
        `riken_sssp_mpit` + `mpi_share.so`), interleaved, so what the timers
        cost is measured beside what they report. Gluon needs no extra run:
        its own sync timer is parsed from every external run.
        """
        rng = random.Random(20260917 + int(self.job))
        selected_tag = f'external-{self.nodes}n-{self.args.workers}w-{self.args.selection_job or self.job}'
        timed = {'adaptive': 'acic-comm', 'riken': 'riken-mpit'}
        for graph in self.args.graphs.split(','):
            path = self.root/'logs'/f'{selected_tag}-{graph}-selected.json'
            if not path.exists():
                print(f'comm_share: no external selection for {graph}; skipped', flush=True)
                continue
            chosen = {c['name']: c for c in json.loads(path.read_text())['arms']}
            arms = []
            for arm, engine in timed.items():
                if arm in chosen:
                    base = dict(chosen[arm])
                    arms += [dict(base, name=arm), dict(base, name=arm+'-timed', engine=engine)]
            test = [x for x in self.references(graph) if x['role'] == 'test'][:self.args.sources]
            order = [(row, c) for row in test for c in arms]
            rng.shuffle(order)
            for row, c in order:
                self.run(graph, int(row['source']), c, row, 'comm-share')

    def external_smoke(self):
        """Every system at every candidate layout once, mid parameters, first
        tuning source. Raises on the first invalid run: this is the gate that
        the Anvil builds, launchers and digests agree before timing anything."""
        for graph in self.args.graphs.split(','):
            row = [x for x in self.references(graph) if x['role'] == 'tune'][0]
            for arm, (layouts, _) in self.external_candidates(graph).items():
                for c in layouts:
                    if not self.run(graph, int(row['source']), c, row, 'external-smoke')['valid']:
                        raise RuntimeError(f'{arm} failed at {c["name"]} on {graph}')

    def progress(self):
        """Replay the recorded losses of progress on both binaries.

        Each case runs its own failing policy on the binary that produced the
        failure and on the repaired one, in the same allocation, in randomized
        order, with and without a delayed round. Delivery delay is included
        because the failures are a property of what the controller can see
        rather than of how fast it sees it, and a stall that survives a delay
        is not a race.

        The mesh case is intermittent -- one query in sixteen -- so it wants
        repetitions rather than reasoning; the RMAT cases failed every replay
        and want fewer. Nothing here produces a performance number: a stalled
        query burns its whole timeout, so the times in this job are not
        comparable to anything.
        """
        rng = random.Random(20260913 + int(self.job))
        cases = [c for c in PROGRESS_CASES
                 if c['nodes'] == self.nodes and c['workers'] == self.args.workers]
        if not cases:
            raise ValueError(f'no recorded failure at {self.nodes} nodes '
                             f'x {self.args.workers} workers')
        for case in cases:
            refs = {int(r['source']): r for r in self.references(case['graph'])}
            for source in case['sources']:
                if source not in refs:
                    raise ValueError(f'source {source} is not one of the '
                                     f'recorded {case["graph"]} sources')
                for rep in range(self.args.reps):
                    order = [(engine, delay)
                             for engine in ['acic', 'acic-progress']
                             for delay in [[], ['--round-delay', '2']]]
                    rng.shuffle(order)
                    for engine, delay in order:
                        config = dict(engine=engine, rpn=case['rpn'],
                                      name=case['name'], flags=case['flags'],
                                      case=case['case'], recorded_job=case['job'],
                                      delayed=bool(delay))
                        self.run(case['graph'], source, config, refs[source],
                                 'progress-replay', rep, extra=delay)

    def tiny(self):
        # Independent Python fixture writer and Dijkstra. Every case creates
        # <1001 updates, including an isolated source and a cross-partition path.
        import heapq
        mask = (1 << 64)-1
        def mix(x):
            x = (x + 0x9E3779B97F4A7C15) & mask
            x = ((x ^ (x >> 30))*0xBF58476D1CE4E5B9) & mask
            x = ((x ^ (x >> 27))*0x94D049BB133111EB) & mask
            return x ^ (x >> 31)
        for kind in ['empty', 'path', 'disconnected']:
            n = 256
            rows = [[] for _ in range(n)]
            if kind != 'empty':
                length = 256 if kind == 'path' else 16
                for i in range(length-1):
                    u, v = (i*73)%n, ((i+1)*73)%n
                    rows[u].append((v, 1+i%11))
                    rows[v].append((u, 1+i%11))
            offsets = [0]
            for row in rows:
                offsets.append(offsets[-1]+len(row))
            graph = 'tiny-'+kind+'-'+self.job
            with (self.root/'graphs'/(graph+'.wsg')).open('wb') as f:
                f.write(struct.pack('=Bqq', 0, offsets[-1], n))
                f.write(struct.pack('='+('q'*(n+1)), *offsets))
                for row in rows:
                    for edge in sorted(row):
                        f.write(struct.pack('=ii', *edge))
            sources = [0, 1] if kind == 'disconnected' else [0]
            for source in sources:
                dist = [mask]*n
                dist[source] = 0
                queue = [(0, source)]
                while queue:
                    d, u = heapq.heappop(queue)
                    if d != dist[u]:
                        continue
                    for v, w in rows[u]:
                        if d+w < dist[v]:
                            dist[v] = d+w
                            heapq.heappush(queue, (d+w, v))
                expected = dict(h1=sum(mix(((v*0x9E3779B97F4A7C15)&mask)^mix(d)) for v, d in enumerate(dist))&mask,
                                h2=sum(mix(((d*0xC2B2AE3D27D4EB4F)&mask)^mix(v+1)) for v, d in enumerate(dist))&mask,
                                reachable=sum(d != mask for d in dist),
                                distance_sum=sum(d for d in dist if d != mask))
                configs = [dict(engine='acic', name=n) for n in ['current', 'old-fixed', 'open']]
                configs += [dict(engine='acic', name='hold', flags=['--combine', 'hold', '--bucket-policy', 'fixed'])]
                for config in configs:
                    r = self.run(graph, source, config, expected, 'termination', extra=['--round-delay', '2', '--verify'])
                    if not r['valid']:
                        raise RuntimeError('Small-component termination regression')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('campaign')
    parser.add_argument('--mode', choices=['smoke', 'tiny', 'benchmark', 'presolve', 'confirm', 'gluon', 'numa', 'finish', 'layout_confirm', 'quiet', 'finish_layout', 'progress', 'controller', 'bimodal', 'deployment', 'width', 'policy', 'external', 'external_smoke', 'comm_share', 'ablation'], default='benchmark')
    parser.add_argument('--graphs', default='uniform20,rmat20,mesh20,rmat22,mesh22,rmat20-s2,uniform20-s2,road-ny,youtube')
    parser.add_argument('--workers', type=int, default=16)
    parser.add_argument('--ranks-per-node', default='1,4', help='RIKEN layout candidates; workers divided among ranks')
    parser.add_argument('--selection-job', help='allocation whose frozen choices are independently confirmed')
    parser.add_argument('--sources', type=int, default=8)
    parser.add_argument('--reps', type=int, default=2)
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--per-graph-width', choices=['on', 'off'], default='on',
                        help='apply PER_GRAPH_WIDTH_RULE to configs that ask for it')
    parser.add_argument('--arms', help='comma-separated config names to keep, for modes that name their arms')
    parser.add_argument('--dev-graphs', default='mesh20,rmat20,uniform20',
                        help='--mode policy: graphs whose tuning sources choose the global fixed setting')
    parser.add_argument('--acic-rpn', type=int, default=1,
                        help='ACIC processes per node; --workers is the per-node total, split among them')
    # 7.6f1 layout candidates, ranks per node except GAPBS (threads, one node).
    # Defaults fit the machine (machine.py): Delta 8,16 / 4,8,16 / 1,8 / 16,64,127;
    # Frontier 8,4 / 2,4,8,56 / 1,8 / 14,28,56.
    parser.add_argument('--acic-layouts', default=machine.layout_defaults()['acic'])
    parser.add_argument('--riken-layouts', default=machine.layout_defaults()['riken'])
    parser.add_argument('--gluon-layouts', default=machine.layout_defaults()['gluon'])
    parser.add_argument('--gap-threads', default=machine.layout_defaults()['gap'])
    # IPDPS sprint: RIKEN and GAPBS deltas are denominator / each divisor. 8g
    # chose the smallest offered (d16) on every RMAT graph and mesh26, so the
    # fair-baseline re-take extends the grid downward.
    parser.add_argument('--delta-divisors', default='64,16,4,1')
    parser.add_argument('--external-no-search', action='store_true',
                        help='external mode: run each arm\'s pinned settings (first layout candidate) on the held-out sources with no warmup or search')
    parser.add_argument('--gluon-binary', default='gluon_sssp',
                        help='gluon_sssp64 (benchmarks/gluon64.patch) for graphs past 2^32 vertices; oec only')
    parser.add_argument('--riken-tolerance', type=float, default=0.0,
                        help='accept a RIKEN run whose distance sum is high by at most this fraction (recorded as exact=False)')
    parser.add_argument('--gluon-partitions', default='oec,cvc',
                        help='Gluon partitioners tried beyond one node')
    parser.add_argument('--gluon-deltas', default='',
                        help='pin the Gluon delta grid (comma list); the layout stage uses the first')
    parser.add_argument('--gluon-layout-divisor', type=int, default=16,
                        help="Gluon layout stage's delta, denominator / this (16: the mid delta; 1 for meshes and roads)")
    parser.add_argument('--gluon-delta-multipliers', default='',
                        help='extra Gluon deltas, denominator x each (e.g. 4,16,64), kept while <= max distance / 4')
    parser.add_argument('--fixed-idle', default='off',
                        help="--mode ablation: idle-flush settings in the fixed search space, 'off' or 'off,on'")
    parser.add_argument('--ablation-binary', default='acic_ipdps',
                        help='--mode ablation: campaign/bin name of the binary under test (NAME_wide for ws24-wide)')
    parser.add_argument('--acic-rpn-map', help='--mode ablation: graph:rpn,... (default --acic-rpn)')
    # tuned-fixed is 7.6f2's question, and Gluon-Sync trailed Async in every
    # 7.5 cell; both remain available by name.
    parser.add_argument('--external-arms', default='adaptive,riken,gluon-async,gap',
                        help='of adaptive, tuned-fixed, riken, gluon-async, gluon-sync, gap')
    args = parser.parse_args()
    campaign = Campaign(args)
    getattr(campaign, args.mode)()
    print('CAMPAIGN COMPLETE', campaign.tag, flush=True)
