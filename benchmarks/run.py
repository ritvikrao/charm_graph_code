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
            if path.name in ['acic', 'acic_quiet', 'acic_progress', 'riken_sssp', 'gap_sssp', 'gluon_sssp']:
                digest = hashlib.sha256()
                with path.open('rb') as f:
                    for chunk in iter(lambda: f.read(1048576), b''):
                        digest.update(chunk)
                self.binary_hashes[str(path)] = digest.hexdigest()

    def run(self, graph, source, config, expected, phase, rep=0, extra=None):
        engine = config['engine']
        workers = self.args.workers
        binary = self.root / 'bin' / {'acic': 'acic', 'acic-quiet': 'acic_quiet', 'acic-progress': 'acic_progress', 'riken': 'riken_sssp', 'gap': 'gap_sssp', 'gluon': 'gluon_sssp'}[engine]
        path = self.root / 'graphs' / (graph + '.wsg')
        env = dict(self.env)
        if 'presolve_seconds' in config:
            env['PRESOL_SECONDS'] = str(config['presolve_seconds'])
        if engine.startswith('acic'):
            pp = ['1.0', '1.0'] if config['name'] == 'open' else ['0.999', '0.005']
            # No +commap below: Reconverse has no communication thread
            # (reconverse/src/cpuaffinity.cpp: "also no commap, we have no
            # commthreads"), so the flag the recorded campaign passed was never
            # parsed. The core past the worker region is still left free for
            # the OS -- the pemap is unchanged -- so these runs are exactly
            # comparable with the recorded ones.
            args = [str(binary), '0', str(path), '1', str(source), '4', *pp,
                    '--result-digest', '--timeout', str(self.args.timeout),
                    *VARIANTS.get(config['name'], config.get('flags', [])),
                    *(extra or []), '+ppn', str(workers), '+pemap', f'0-{workers-1}',
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
                      hosts=os.environ.get('SLURM_JOB_NODELIST'), command=cmd)
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
            'read_seconds': r'^Read time: ([\d.eE+-]+)',
            'total_seconds': r'^Total time: ([\d.eE+-]+)',
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
            selected += [dict(engine='acic', name=n) for n in ['current', 'control', 'old-fixed', 'open']]
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
    parser.add_argument('--mode', choices=['smoke', 'tiny', 'benchmark', 'presolve', 'confirm', 'gluon', 'numa', 'finish', 'layout_confirm', 'quiet', 'finish_layout', 'progress'], default='benchmark')
    parser.add_argument('--graphs', default='uniform20,rmat20,mesh20,rmat22,mesh22,rmat20-s2,uniform20-s2,road-ny,youtube')
    parser.add_argument('--workers', type=int, default=16)
    parser.add_argument('--ranks-per-node', default='1,4', help='RIKEN layout candidates; workers divided among ranks')
    parser.add_argument('--selection-job', help='allocation whose frozen choices are independently confirmed')
    parser.add_argument('--sources', type=int, default=8)
    parser.add_argument('--reps', type=int, default=2)
    parser.add_argument('--timeout', type=int, default=120)
    args = parser.parse_args()
    campaign = Campaign(args)
    getattr(campaign, args.mode)()
    print('CAMPAIGN COMPLETE', campaign.tag, flush=True)
