#!/usr/bin/env python3
"""Paired, independently checked SSSP comparisons inside a Slurm allocation."""
import argparse
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

# Step 7.6d measured the two width rules on all nine graphs at one node with
# 120 workers (job 22061958). `weight` is not a default -- it costs 1.4x on
# rmat22, 1.7x on youtube, 2.4x on rmat20 and 2.9x on rmat20-s2, which are
# exactly the graphs whose adaptive coarsening reaches bucket scale 7-10 on
# its own and therefore does not need it. It is worth 2.73x on road-ny, on
# 4/4 held-out sources, which is one of the three graphs stuck at scale 1.
# So it is a per-case setting, named here by graph rather than derived, and
# applied only to configs that ask for it (`per_graph_width`). Two things
# keep it honest: the flag lands in `command` on every row, and the width the
# run actually bucketed with is parsed back as `bucket_width`.
#
# CAVEAT, and it is the reason this map has one entry rather than three: the
# measurement is one allocation. The same over-width on rmat22 reads 0.98x at
# 128 workers over eight nodes and 1.4x slower at 120 workers on one, so the
# sign of this effect is known to move with worker density. mesh20 and mesh22
# are left out because at 1.01x and 1.05x they are inside that uncertainty.
PER_GRAPH_WIDTH_RULE = {'road-ny': 'weight'}


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
        self.env = dict(os.environ, PMI_MAX_KVS_ENTRIES='1000',
                        FI_CXI_RX_MATCH_MODE='hybrid', NO_AFFINITY='1',
                        OMP_PROC_BIND='close', OMP_PLACES='cores')
        self.count = 0
        self.binary_hashes = {}
        for path in (self.root/'bin').iterdir():
            if path.name in ['acic', 'acic_quiet', 'acic_progress', 'acic_shm', 'acic_width', 'riken_sssp', 'gap_sssp', 'gluon_sssp']:
                digest = hashlib.sha256()
                with path.open('rb') as f:
                    for chunk in iter(lambda: f.read(1048576), b''):
                        digest.update(chunk)
                self.binary_hashes[str(path)] = digest.hexdigest()

    def run(self, graph, source, config, expected, phase, rep=0, extra=None):
        engine = config['engine']
        workers = self.args.workers
        per_graph_rule = None
        binary = self.root / 'bin' / {'acic': 'acic', 'acic-quiet': 'acic_quiet', 'acic-progress': 'acic_progress', 'acic-shm': 'acic_shm', 'acic-width': 'acic_width', 'riken': 'riken_sssp', 'gap': 'gap_sssp', 'gluon': 'gluon_sssp'}[engine]
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
                    *flags, '+ppn', str(workers), '+pemap', f'0-{workers-1}',
                    '+lci_ndevices', '4']
            launch = ['srun', '-N', str(self.nodes), '-n', str(self.nodes),
                      '--ntasks-per-node=1', '--cpu-bind=none']
            if config.get('rpn', 1) != 1:
                ranks = config['rpn']
                # Replace the trailing runtime flags with per-process maps.
                args = ['bash', str(Path(__file__).with_name('launch_acic.sh')),
                        str(ranks), str(workers), *args[:-6]]
                launch = ['srun', '-N', str(self.nodes), '-n', str(self.nodes*ranks),
                          '--ntasks-per-node', str(ranks), '-c', str(128//ranks), '--cpu-bind=none']
        elif engine == 'gluon':
            launch = ['srun', '-N', str(self.nodes), '-n', str(self.nodes),
                      '--ntasks-per-node=1', '-c', str(workers+1), '--cpu-bind=cores']
            args = [str(binary), str(path.with_suffix('.gr')), '-symmetricGraph',
                    '-exec='+config['model'], '-startNode='+str(source), '-t='+str(workers),
                    '-runs=1', '-maxIterations=2147483647', '-delta='+str(config['delta']),
                    '-partition='+config['partition']]
        else:
            ranks = config.get('rpn', 1)
            threads = workers // ranks
            env['OMP_NUM_THREADS'] = str(threads)
            launch = ['srun', '-N', str(self.nodes), '-n', str(self.nodes*ranks),
                      '--ntasks-per-node', str(ranks), '-c', str(threads), '--cpu-bind=cores']
            args = [str(binary), str(path), str(source), str(config['delta'])]
            if engine == 'riken':
                args += [str(config['denominator']), str(config.get('presolve', 0))]
            elif self.nodes != 1:
                raise ValueError('GAPBS is a single-node baseline')
        cmd = [*launch, '--unbuffered', '--kill-on-bad-exit=1', *args]
        self.count += 1
        record = dict(job=self.job, nodes=self.nodes, workers=workers,
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
                output, _ = process.communicate(timeout=self.args.timeout+90)
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
        t = re.search(r'^Compute time: ([\d.eE+-]+)', output, re.M) if engine.startswith('acic') else re.search(r'^BENCH .*?\bsolve_seconds=([\d.eE+-]+)', output, re.M)
        if t:
            record['seconds'] = float(t[1])
            record['valid'] = record['valid'] and math.isfinite(record['seconds']) and record['seconds'] > 0
        else:
            record['valid'] = False
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
        }.items():
            value = re.search(regex, output, re.M)
            if value:
                record[field] = float(value[1])
        with gzip.open(self.raw, 'at') as f:
            f.write('\nRUN ' + json.dumps(record) + '\n' + output)
        with self.records.open('a') as f:
            f.write(json.dumps(record) + '\n')
        print(f'{self.tag} #{self.count} {phase} {graph} src={source} {config} '
              f'valid={record["valid"]} seconds={record.get("seconds")}', flush=True)
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
    parser.add_argument('--mode', choices=['smoke', 'tiny', 'benchmark', 'presolve', 'confirm', 'gluon', 'numa', 'finish', 'layout_confirm', 'quiet', 'finish_layout', 'progress', 'controller', 'bimodal', 'deployment', 'width'], default='benchmark')
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
    parser.add_argument('--acic-rpn', type=int, default=1,
                        help='ACIC processes per node; --workers is the per-node total, split among them')
    args = parser.parse_args()
    campaign = Campaign(args)
    getattr(campaign, args.mode)()
    print('CAMPAIGN COMPLETE', campaign.tag, flush=True)
