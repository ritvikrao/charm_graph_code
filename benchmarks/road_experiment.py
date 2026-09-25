#!/usr/bin/env python3
"""Shared runner for the bounded Delta road layout and queue experiments."""
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess

from check_onenode_digest import check
from onenode_report import fields
from work_cost_report import check_work_accounting, production_attempts
from road_layout_launch import core_maps


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def validate(reference, row):
    log = Path(row['log'])
    check(reference, row['source'], log)
    t = log.read_text()
    wasp = row['binary'] == 'wasp_sssp'
    pattern = r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)' if wasp else r'Compute time: ([0-9.eE+-]+)'
    times = re.findall(pattern, t)
    assert len(times) == 1 and math.isfinite(float(times[0])) and float(times[0]) > 0
    result = dict(seconds=float(times[0]))
    if wasp:
        return result
    layout = row['layout']
    ranks, ppn = layout['ranks'], layout['ppn']
    assert f'Starting Reconverse with {ranks} processes, {ranks*ppn} PEs' in t
    assert 'Using the original scheduler (+old-scheduler)' in t
    assert 'Process sharing: on' in t
    maps = core_maps(layout)
    # Runtime prints this only after successful affinity binding.
    pins = [(int(pe), int(rank), int(cpu)) for pe,rank,cpu in
            re.findall(r'set PE (\d+) on node (\d+) to PU P#(\d+)', t)]
    expected = [(rank*ppn+pe, rank, cpu) for rank,cpus in enumerate(maps)
                for pe,cpu in enumerate(cpus)]
    assert sorted(pins) == expected, ('affinity', len(pins), len(expected))
    result.update(edge_attempts=production_attempts(t, 23947347),
                  rounds=t.count('Updates: created:'), affinity_verified=True)
    result['scan_factor'] = result['edge_attempts']/57708624
    if row.get('work_cost'):
        c = check_work_accounting(t, 23947347)
        result['counters'] = c
        result['comm'] = fields(t, 'COMM_SHARE')
        clock = fields(t, 'WORK_CLOCK')
        result['queue'] = {}
        for kind in ('push', 'pop'):
            if c[kind+'_samples']:
                mean = c[kind+'_ticks']/c[kind+'_samples']
                result['queue'][kind] = dict(ns=mean*clock['window_us']*1000/clock['window_ticks'],
                    estimated_share=mean*c[kind+'_calls']/clock['window_ticks'])
    return result


class Study:
    def __init__(self, campaign, kind, protocol_name):
        self.root = Path(campaign).resolve()
        self.app = Path(__file__).resolve().parents[1]
        self.protocol = json.loads((self.root/'protocol'/protocol_name).read_text())
        self.reference = self.root/'graphs/road-usa-z.reference.txt'
        refs = [r.split() for r in self.reference.read_text().splitlines() if r[:1].isdigit()]
        self.train = [r[0] for r in refs if r[1]=='tune'][:2]
        self.test = [r[0] for r in refs if r[1]=='test'][:4]
        assert len(self.train)==2 and len(self.test)==4
        assert int(os.environ['SLURM_NNODES']) == 1
        self.job = os.environ['SLURM_JOB_ID']
        self.out = self.root/'logs'/(kind+'-'+self.job)
        self.out.mkdir()
        self.rows = []
        self.rng = random.Random(self.protocol['order_seed'])
        runtime = json.loads((self.root/'protocol/runtime-manifest.json').read_text())
        for path, digest in runtime['libraries'].items():
            assert sha256(path)==digest, ('runtime changed', path)
        self.manifest = dict(job=self.job, hosts=os.environ['SLURM_JOB_NODELIST'],
            protocol=self.protocol, runtime=runtime,
            binaries={p.name:sha256(p) for p in (self.root/'bin').iterdir() if not p.name.endswith('.manifest')},
            graphs={suffix:sha256(self.root/'graphs'/('road-usa-z'+suffix))
                    for suffix in ('.wsg','.meta','.reference.txt')},
            harness={p.name:sha256(p) for p in (self.app/'benchmarks').glob('road_*.*')},
            build_manifests={p.name:p.read_text() for p in (self.root/'bin').glob('*.manifest')})
        self.write('manifest.json', self.manifest)
        for name, cmd in [('lscpu.txt',['lscpu']), ('numa.txt',['numactl','--hardware'])]:
            with (self.out/name).open('w') as f: subprocess.run(cmd, stdout=f, check=True)

    def write(self, name, data):
        (self.out/name).write_text(json.dumps(data, indent=2)+'\n')

    def run(self, arm, source, phase, rep=0):
        index = len(self.rows)
        label = arm['label']
        log = self.out/f'{index:03d}-{phase}-{label}-s{source}-r{rep}.out'
        env = dict(os.environ, OMP_DYNAMIC='FALSE', OMP_PLACES='cores', OMP_PROC_BIND='close')
        cmd = ['srun','-N1','--cpu-bind=none','--unbuffered','--kill-on-bad-exit=1','--time=5']
        if arm['binary']=='wasp_sssp':
            env['OMP_NUM_THREADS'] = str(arm['threads'])
            cmd += ['-n1','-c128',str(self.root/'bin/wasp_sssp'),
                    str(self.root/'graphs/road-usa-z.wsg'),source,str(arm['delta'])]
        else:
            layout = arm['layout']
            ranks = layout['ranks']
            cmd += [f'-n{ranks}',f'--ntasks-per-node={ranks}',f'-c{128//ranks}',
                    'python3',str(self.app/'benchmarks/road_layout_launch.py'),json.dumps(layout),
                    str(self.root/'bin'/arm['binary']), '0',str(self.root/'graphs/road-usa-z.wsg'),
                    '1',source,'4','0.999','0.005','--result-digest','--timeout','60']
            cmd += self.protocol['common_flags'] + arm.get('flags', [])
        row = dict(index=index, **arm, source=source, phase=phase, rep=rep,
                   command=cmd, log=str(log), valid=False)
        try:
            with log.open('w') as f:
                row['returncode'] = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT,
                                                   env=env, timeout=360).returncode
            assert row['returncode']==0, row['returncode']
            row.update(validate(self.reference, row))
            row['valid'] = True
        except Exception as error:
            row['error'] = repr(error)
        self.rows.append(row)
        with (self.out/'runs.jsonl').open('a') as f: f.write(json.dumps(row)+'\n')
        print(index, phase, label, source, row.get('seconds'),
              'PASS' if row['valid'] else row['error'], flush=True)
        if not row['valid']: raise RuntimeError(f'Invalid solve: {log}: {row["error"]}')
        return row

    def block(self, arms, sources, phase, rep):
        cells = [(a,s) for s in sources for a in arms]
        self.rng.shuffle(cells)
        for arm, source in cells: self.run(arm,source,phase,rep)

    def score(self, arm, phase):
        values = [statistics.median(r['seconds'] for r in self.rows
                  if r['label']==arm['label'] and r['phase']==phase and r['source']==source)
                  for source in self.train]
        return statistics.geometric_mean(values)

    def finish(self, arms, selection, expected):
        assert len(self.rows)==expected and all(r['valid'] for r in self.rows)
        seen = set()
        for row in self.rows:
            key = (row['label'],row['source'],row['phase'],row['rep'])
            assert key not in seen
            seen.add(key)
            for k,v in validate(self.reference,row).items(): assert row[k]==v
        sources = []
        for source in self.test:
            values = {}
            for arm in arms:
                rows = [r for r in self.rows if r['source']==source and r['label']==arm['label'] and r['phase']=='timing']
                assert len(rows)==3
                values[arm['label']] = dict(median=statistics.median(r['seconds'] for r in rows),
                    seconds=[r['seconds'] for r in rows], scans=[r.get('scan_factor') for r in rows],
                    rounds=[r.get('rounds') for r in rows])
            sources.append(dict(source=source, variants=values))
        self.write('summary.json', dict(manifest=self.manifest, validated_solves=len(self.rows),
            selection=selection, sources=sources, work=[r for r in self.rows if r['phase']=='work']))
        print('COMPLETE', self.out, len(self.rows), flush=True)
