#!/usr/bin/env python3
"""One-node ACIC/Wasp screen: native road, quarter-weight road, similar-size mesh."""
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
import sys

from check_onenode_digest import check
from work_cost_report import production_attempts


APP = Path(__file__).resolve().parents[1]
PROTOCOL = json.loads((APP / 'benchmarks/delta-road-gap-protocol.json').read_text())
RNG = random.Random(20260925)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def sources(reference, role, limit):
    rows = (line.split() for line in reference.read_text().splitlines()
            if line[:1].isdigit())
    chosen = [row[0] for row in rows if row[1] == role][:limit]
    assert len(chosen) == limit, (reference, role, chosen)
    return chosen


class Probe:
    def __init__(self, root):
        assert int(os.environ['SLURM_NNODES']) == 1
        self.root = Path(root).resolve()
        self.out = self.root / ('probe-' + os.environ['SLURM_JOB_ID'])
        self.out.mkdir(parents=True)
        self.graphs = Path(PROTOCOL['graphs_root'])
        self.bins = Path(PROTOCOL['binaries_root'])
        self.rows = []
        self.manifest = dict(job=os.environ['SLURM_JOB_ID'],
                             hosts=os.environ['SLURM_JOB_NODELIST'],
                             protocol=PROTOCOL,
                             app_revision=subprocess.check_output(
                                 ['git', '-C', str(APP), 'rev-parse', 'HEAD'], text=True).strip(),
                             binaries={name: sha256(self.bins / name)
                                       for name in (PROTOCOL['acic_binary'],
                                                    PROTOCOL['wasp_binary'])},
                             graphs={g['name']: dict(
                                 wsg_bytes=(self.graphs / (g['name'] + '.wsg')).stat().st_size,
                                 reference_sha256=sha256(self.graphs / (g['name'] + '.reference.txt')))
                                 for g in PROTOCOL['graphs']})
        (self.out / 'manifest.json').write_text(json.dumps(self.manifest, indent=2) + '\n')

    def run(self, graph, source, engine, phase, rep, *, threads=None, delta=None):
        name = graph['name']
        index = len(self.rows)
        log = self.out / f'{index:03d}-{phase}-{name}-{engine}-s{source}-r{rep}.out'
        env = dict(os.environ, OMP_DYNAMIC='FALSE', OMP_PLACES='cores',
                   OMP_PROC_BIND='close')
        cmd = ['srun', '-N1', '--cpu-bind=none', '--unbuffered',
               '--kill-on-bad-exit=1', '--time=5']
        if engine == 'wasp':
            env['OMP_NUM_THREADS'] = str(threads)
            cmd += ['-n1', '-c128', str(self.bins / PROTOCOL['wasp_binary']),
                    str(self.graphs / (name + '.wsg')), source, str(delta)]
        else:
            cmd += ['-n16', '--ntasks-per-node=16', '-c8', 'bash',
                    str(APP / 'benchmarks/launch_acic.sh'), '16', '112',
                    str(self.bins / PROTOCOL['acic_binary']), '0',
                    str(self.graphs / (name + '.wsg')), '1', source,
                    '4', '0.999', '0.005', '--result-digest', '--timeout', '180',
                    '--process-share', 'auto', '--reader-tile', 'auto',
                    '--slack-control', 'off', '--process-queue', 'nearest',
                    '--process-queue-batch', '8', '--hub-hints', 'auto',
                    '+old-scheduler', '--heap-slice', '8']
            if graph['bucket_width'] is not None:
                cmd += ['--bucket-width', str(graph['bucket_width'])]
        row = dict(index=index, graph=name, source=source, engine=engine,
                   phase=phase, rep=rep, threads=threads, delta=delta,
                   command=cmd, log=str(log), valid=False)
        with log.open('w') as stream:
            result = subprocess.run(cmd, stdout=stream, stderr=subprocess.STDOUT,
                                    env=env, timeout=360)
        row['returncode'] = result.returncode
        try:
            assert result.returncode == 0, result.returncode
            check(self.graphs / (name + '.reference.txt'), source, log)
            text = log.read_text()
            pattern = (r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)'
                       if engine == 'wasp' else r'Compute time: ([0-9.eE+-]+)')
            matches = re.findall(pattern, text)
            assert len(matches) == 1
            row['seconds'] = float(matches[0])
            assert math.isfinite(row['seconds']) and row['seconds'] > 0
            if engine == 'acic':
                assert 'Starting Reconverse with 16 processes, 112 PEs' in text
                assert 'Using the original scheduler (+old-scheduler)' in text
                assert 'Process queue: nearest' in text
                assert 'Heap slice: 8' in text
                if graph['bucket_width'] is not None:
                    assert re.search(r'^Bucket width: ' + str(graph['bucket_width']) + r'(?:\.0+)?,',
                                     text, re.M)
                vertices = int((self.graphs / (name + '.meta')).read_text().split()[0].split('=')[1])
                row['edge_attempts'] = production_attempts(text, vertices)
                row['rounds'] = text.count('Updates: created:')
            row['valid'] = True
        except Exception as error:
            row['error'] = repr(error)
        self.rows.append(row)
        with (self.out / 'runs.jsonl').open('a') as stream:
            stream.write(json.dumps(row) + '\n')
        print(index, phase, name, engine, source, threads, delta,
              row.get('seconds'), 'PASS' if row['valid'] else row.get('error'),
              flush=True)
        return row

    def tune(self, graph):
        if 'wasp_selected' in graph:
            return graph['wasp_selected']
        arms = list(itertools.product(graph['wasp_threads'], graph['wasp_deltas']))
        train = sources(self.graphs / (graph['name'] + '.reference.txt'), 'tune', 2)
        for rep in range(PROTOCOL['wasp_training_repetitions']):
            cells = [(source, threads, delta) for source in train
                     for threads, delta in arms]
            RNG.shuffle(cells)
            for source, threads, delta in cells:
                self.run(graph, source, 'wasp', 'tune', rep,
                         threads=threads, delta=delta)
        scores = {}
        for threads, delta in arms:
            values = [[row['seconds'] for row in self.rows
                       if row['valid'] and row['graph'] == graph['name']
                       and row['phase'] == 'tune' and row['source'] == source
                       and row['threads'] == threads and row['delta'] == delta]
                      for source in train]
            scores[f'{threads}x{delta}'] = (statistics.geometric_mean(
                statistics.median(v) for v in values) if all(values) else None)
        valid_arms = [arm for arm in arms if scores[f'{arm[0]}x{arm[1]}'] is not None]
        assert valid_arms, ('No valid Wasp arm', graph['name'])
        winner = min(valid_arms, key=lambda arm: scores[f'{arm[0]}x{arm[1]}'])
        print('SELECTED', graph['name'], winner, scores, flush=True)
        return dict(threads=winner[0], delta=winner[1], scores=scores)

    def main(self):
        selected = {}
        for graph in PROTOCOL['graphs']:
            chosen = self.tune(graph)
            selected[graph['name']] = chosen
            test = sources(self.graphs / (graph['name'] + '.reference.txt'), 'test', 2)
            for rep in range(-PROTOCOL['test_warmups'],
                             PROTOCOL['test_repetitions']):
                cells = [(source, engine) for source in test
                         for engine in ('acic', 'wasp')]
                RNG.shuffle(cells)
                for source, engine in cells:
                    row = self.run(graph, source, engine,
                                   'warmup' if rep < 0 else 'test', rep,
                                   threads=chosen['threads'] if engine == 'wasp' else None,
                                   delta=chosen['delta'] if engine == 'wasp' else None)
                    if not row['valid']:
                        raise RuntimeError('Invalid test solve: ' + str(row['log']))
        results = {}
        for graph in PROTOCOL['graphs']:
            name = graph['name']
            test = sources(self.graphs / (name + '.reference.txt'), 'test', 2)
            per_source = {}
            for source in test:
                cells = {}
                for engine in ('acic', 'wasp'):
                    rows = [r for r in self.rows if r['graph'] == name
                            and r['source'] == source and r['engine'] == engine
                            and r['phase'] == 'test']
                    assert len(rows) == PROTOCOL['test_repetitions'] and all(r['valid'] for r in rows)
                    cells[engine] = dict(seconds=statistics.median(r['seconds'] for r in rows),
                                         runs=[r['seconds'] for r in rows])
                    if engine == 'acic':
                        cells[engine]['rounds'] = statistics.median(r['rounds'] for r in rows)
                        cells[engine]['edge_attempts'] = statistics.median(r['edge_attempts'] for r in rows)
                per_source[source] = dict(**cells,
                    speedup_over_wasp=cells['wasp']['seconds']/cells['acic']['seconds'])
            results[name] = dict(selected_wasp=selected[name], sources=per_source,
                                 speedup_geomean=statistics.geometric_mean(
                                     item['speedup_over_wasp'] for item in per_source.values()))
        summary = dict(manifest=self.manifest, validated_solves=sum(r['valid'] for r in self.rows),
                       invalid=[r for r in self.rows if not r['valid']], results=results)
        (self.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        assert not summary['invalid']
        print('COMPLETE', self.out, summary['validated_solves'], flush=True)


if __name__ == '__main__':
    Probe(sys.argv[1]).main()
