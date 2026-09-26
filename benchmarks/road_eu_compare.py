#!/usr/bin/env python3
"""Bounded, digest-checked one-node Delta ACIC/Wasp comparison on road-eu-z."""
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
PROTOCOL = json.loads((APP / 'benchmarks/delta-road-eu-compare-protocol.json').read_text())
RNG = random.Random(20260925)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def sources(reference, role, count):
    rows = [row.split() for row in reference.read_text().splitlines()
            if row[:1].isdigit()]
    found = [row[0] for row in rows if row[1] == role][:count]
    assert len(found) == count, (reference, role, found)
    return found


def median_for(rows, **match):
    found = [row['seconds'] for row in rows
             if all(row.get(key) == value for key, value in match.items())]
    assert len(found) == PROTOCOL['training_repetitions'], (match, found)
    return statistics.median(found)


class Compare:
    def __init__(self, root):
        assert int(os.environ['SLURM_NNODES']) == 1
        self.root = Path(root).resolve()
        self.out = self.root / ('compare-' + os.environ['SLURM_JOB_ID'])
        self.out.mkdir(parents=True)
        self.graphs = Path(PROTOCOL['graphs_root'])
        self.bins = Path(PROTOCOL['binaries_root'])
        self.name = PROTOCOL['graph']
        self.graph = self.graphs / f'{self.name}.wsg'
        self.reference = self.graphs / f'{self.name}.reference.txt'
        meta = (self.graphs / f'{self.name}.meta').read_text()
        self.vertices = int(re.search(r'vertices=(\d+)', meta)[1])
        assert self.graph.stat().st_size > 0 and self.reference.stat().st_size > 0
        self.rows = []
        self.manifest = dict(job=os.environ['SLURM_JOB_ID'],
                             host=os.environ['SLURM_JOB_NODELIST'],
                             protocol=PROTOCOL,
                             app_revision=subprocess.check_output(
                                 ['git', '-C', str(APP), 'rev-parse', 'HEAD'],
                                 text=True).strip(),
                             graph=dict(path=str(self.graph),
                                        bytes=self.graph.stat().st_size,
                                        sha256=sha256(self.graph),
                                        meta=meta.strip(),
                                        reference_sha256=sha256(self.reference)),
                             binaries={name: sha256(self.bins / name)
                                       for name in (PROTOCOL['acic_binary'],
                                                    PROTOCOL['wasp_binary'])})
        (self.out / 'manifest.json').write_text(json.dumps(self.manifest, indent=2) + '\n')

    def run(self, source, arm, phase, rep, *, width=None, threads=None, delta=None):
        index = len(self.rows)
        log = self.out / f'{index:03d}-{phase}-{arm}-s{source}-r{rep}.out'
        env = dict(os.environ, OMP_DYNAMIC='FALSE', OMP_PLACES='cores',
                   OMP_PROC_BIND='close')
        cmd = ['srun', '-N1', '--cpu-bind=none', '--unbuffered',
               '--kill-on-bad-exit=1', '--time=5']
        if arm == 'wasp':
            env['OMP_NUM_THREADS'] = str(threads)
            cmd += ['-n1', '-c128', str(self.bins / PROTOCOL['wasp_binary']),
                    str(self.graph), source, str(delta)]
        else:
            cmd += ['-n16', '--ntasks-per-node=16', '-c8', 'bash',
                    str(APP / 'benchmarks/launch_acic.sh'), '16', '112',
                    str(self.bins / PROTOCOL['acic_binary']), '0',
                    str(self.graph), '1', source, '4', '0.999', '0.005',
                    '--result-digest', '--timeout', '180',
                    '--process-share', 'auto', '--reader-tile', 'auto',
                    '--slack-control', 'off', '--process-queue', 'nearest',
                    '--process-queue-batch', '8', '--hub-hints', 'auto',
                    '+old-scheduler', '--heap-slice', '8',
                    '--bucket-width', str(width)]
        row = dict(index=index, source=source, arm=arm, phase=phase, rep=rep,
                   width=width, threads=threads, delta=delta,
                   command=cmd, log=str(log), valid=False)
        try:
            with log.open('w') as stream:
                result = subprocess.run(cmd, stdout=stream, stderr=subprocess.STDOUT,
                                        env=env, timeout=360)
            row['returncode'] = result.returncode
            assert result.returncode == 0, result.returncode
            check(self.reference, source, log)
            output = log.read_text()
            pattern = (r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)'
                       if arm == 'wasp' else r'Compute time: ([0-9.eE+-]+)')
            matches = re.findall(pattern, output)
            assert len(matches) == 1, matches
            row['seconds'] = float(matches[0])
            assert math.isfinite(row['seconds']) and row['seconds'] > 0
            if arm != 'wasp':
                assert 'Starting Reconverse with 16 processes, 112 PEs' in output
                assert 'Using the original scheduler (+old-scheduler)' in output
                assert 'Process queue: nearest' in output
                assert 'Heap slice: 8' in output
                assert re.search(r'^Bucket width: ' + str(width) + r'(?:\.0+)?,',
                                 output, re.M)
                row['rounds'] = output.count('Updates: created:')
                row['edge_attempts'] = production_attempts(output, self.vertices)
            row['valid'] = True
        except Exception as error:
            row['error'] = repr(error)
        self.rows.append(row)
        with (self.out / 'runs.jsonl').open('a') as stream:
            stream.write(json.dumps(row) + '\n')
        print(index, phase, arm, source, width, threads, delta,
              row.get('seconds'), 'PASS' if row['valid'] else row.get('error'),
              flush=True)
        if not row['valid']:
            raise RuntimeError(f'Invalid solve: {log}: {row.get("error")}')
        return row

    def tune(self):
        train = sources(self.reference, 'tune', PROTOCOL['training_sources'])
        wasp = list(itertools.product(PROTOCOL['wasp_threads'],
                                     PROTOCOL['wasp_deltas']))
        for rep in range(PROTOCOL['training_repetitions']):
            cells = [(source, 'acic', width, None, None) for source in train
                     for width in PROTOCOL['acic_widths']]
            cells += [(source, 'wasp', None, threads, delta) for source in train
                      for threads, delta in wasp]
            RNG.shuffle(cells)
            for source, arm, width, threads, delta in cells:
                self.run(source, arm, 'tune', rep, width=width,
                         threads=threads, delta=delta)
        scores = {'acic': {}, 'wasp': {}}
        for width in PROTOCOL['acic_widths']:
            scores['acic'][str(width)] = statistics.geometric_mean(
                median_for(self.rows, source=source, arm='acic',
                           phase='tune', width=width) for source in train)
        for threads, delta in wasp:
            scores['wasp'][f'{threads}x{delta}'] = statistics.geometric_mean(
                median_for(self.rows, source=source, arm='wasp',
                           phase='tune', threads=threads, delta=delta)
                for source in train)
        width = min(PROTOCOL['acic_widths'], key=lambda value: scores['acic'][str(value)])
        threads, delta = min(wasp, key=lambda pair: scores['wasp'][f'{pair[0]}x{pair[1]}'])
        selection = dict(acic_width=width, wasp_threads=threads,
                         wasp_delta=delta, scores=scores,
                         training_sources=train)
        (self.out / 'selection.json').write_text(json.dumps(selection, indent=2) + '\n')
        print('SELECTED', selection, flush=True)
        return selection

    def main(self):
        selection = self.tune()
        test = sources(self.reference, 'test', PROTOCOL['test_sources'])
        for rep in range(-PROTOCOL['test_warmups'], PROTOCOL['test_repetitions']):
            cells = [(source, arm) for source in test for arm in PROTOCOL['test_arms']]
            RNG.shuffle(cells)
            for source, arm in cells:
                self.run(source, arm, 'warmup' if rep < 0 else 'test', rep,
                         width=selection['acic_width'] if arm != 'wasp' else None,
                         threads=selection['wasp_threads'] if arm == 'wasp' else None,
                         delta=selection['wasp_delta'] if arm == 'wasp' else None)
        results = {}
        for source in test:
            cells = {}
            for arm in PROTOCOL['test_arms']:
                rows = [row for row in self.rows if row['phase'] == 'test'
                        and row['source'] == source and row['arm'] == arm]
                assert len(rows) == PROTOCOL['test_repetitions']
                cells[arm] = dict(seconds=statistics.median(row['seconds'] for row in rows),
                                  runs=[row['seconds'] for row in rows])
                if arm != 'wasp':
                    cells[arm]['rounds'] = statistics.median(row['rounds'] for row in rows)
                    cells[arm]['edge_attempts'] = statistics.median(
                        row['edge_attempts'] for row in rows)
            cells['speedup_over_wasp'] = cells['wasp']['seconds'] / cells['acic']['seconds']
            cells['control_ratio'] = cells['acic_control']['seconds'] / cells['acic']['seconds']
            results[source] = cells
        summary = dict(manifest=self.manifest, selection=selection,
                       valid_solves=len(self.rows), results=results,
                       speedup_geomean=statistics.geometric_mean(
                           result['speedup_over_wasp'] for result in results.values()))
        (self.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        print('COMPLETE', self.out, summary['valid_solves'], flush=True)


if __name__ == '__main__':
    Compare(sys.argv[1]).main()
