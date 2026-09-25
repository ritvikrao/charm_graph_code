#!/usr/bin/env python3
"""Frozen chunk/slice regression check and training-selected Wasp comparison."""
import argparse
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

from check_onenode_digest import check
from onenode_report import fields
from work_cost_report import check_work_accounting, production_attempts


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign', type=Path)
    args = ap.parse_args()
    root = args.campaign.resolve()
    app = Path(__file__).resolve().parents[1]
    protocol = json.loads((root/'protocol/protocol.json').read_text())
    assert int(os.environ['SLURM_NNODES']) == 1
    job = os.environ['SLURM_JOB_ID']
    out = root/'logs'/('compare-'+job)
    out.mkdir()
    manifest = dict(job=job, hosts=os.environ['SLURM_JOB_NODELIST'], protocol=protocol,
                    binaries={p.name:sha256(p) for p in (root/'bin').iterdir()
                              if not p.name.endswith('.manifest')}, graphs={},
                    runtime=json.loads((root/'protocol/runtime-manifest.json').read_text()))
    for spec in protocol['graphs']:
        graph = spec['name']
        manifest['graphs'][graph] = {
            suffix:sha256(root/'graphs'/(graph+suffix))
            for suffix in ['.wsg', '.meta', '.reference.txt']}
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    with (out/'lscpu.txt').open('w') as f:
        subprocess.run(['lscpu'], stdout=f, check=True)
    with (out/'numa.txt').open('w') as f:
        subprocess.run(['numactl', '--hardware'], stdout=f, check=True)
    records = []
    summaries = {}
    rng = random.Random(protocol['order_seed'])

    for spec in protocol['graphs']:
        graph = spec['name']
        reference = root/'graphs'/(graph+'.reference.txt')
        refs = [s.split() for s in reference.read_text().splitlines() if s[:1].isdigit()]
        train = [r for r in refs if r[1]=='tune'][:2]
        test = [r for r in refs if r[1]=='test'][:4]
        assert len(train)==2 and len(test)==4
        variants = spec['variants']

        def launch(variant, ref, phase, rep=0, setting=None):
            index = len(records)
            source = ref[0]
            label = f'{index:03d}-{graph}-{phase}-{variant["label"]}-s{source}-r{rep}'
            log = out/(label+'.out')
            env = dict(os.environ, OMP_DYNAMIC='FALSE', OMP_PLACES='cores', OMP_PROC_BIND='close')
            cmd = ['srun', '-N1', '--cpu-bind=none', '--unbuffered',
                   '--kill-on-bad-exit=1', '--time=5']
            wasp = variant['label']=='wasp'
            if wasp:
                env['OMP_NUM_THREADS'] = str(setting['threads'])
                cmd += ['-n1', '-c128', str(root/'bin/wasp_sssp'),
                        str(root/'graphs'/(graph+'.wsg')), source, str(setting['delta'])]
            else:
                cmd += ['-n16', '--ntasks-per-node=16', '-c8', 'bash',
                        str(app/'benchmarks/launch_acic.sh'), '16', '112',
                        str(root/'bin'/variant['binary']), '0',
                        str(root/'graphs'/(graph+'.wsg')), '1', source, '4', '0.999', '0.005',
                        '--result-digest', '--timeout', '60'] + variant['flags']
            row = dict(index=index, graph=graph, label=variant['label'], binary=variant['binary'],
                       phase=phase, rep=rep, source=source, role=ref[1], setting=setting,
                       command=cmd, log=str(log), valid=False)
            try:
                with log.open('w') as f:
                    rc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT,
                                        env=env, timeout=360).returncode
                row['returncode'] = rc
                if rc: raise ValueError(f'launch exit {rc}')
                check(reference, source, log)
                text = log.read_text()
                pattern = (r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)' if wasp
                           else r'Compute time: ([0-9.eE+-]+)')
                times = re.findall(pattern, text)
                assert len(times)==1 and math.isfinite(float(times[0])) and float(times[0])>0
                row['seconds'] = float(times[0])
                if not wasp:
                    row['edge_attempts'] = production_attempts(text, spec['vertices'])
                    row['scan_factor'] = row['edge_attempts']/spec['stored_directed_edges']
                    row['rounds'] = text.count('Updates: created:')
                    row['process_sharing'] = re.search(r'^Process sharing: (on|off)', text, re.M)[1]
                    assert row['process_sharing']==spec['expected_process_sharing']
                    row['heap_slice_line'] = re.search(r'^Heap slice: .*', text, re.M)[0]
                    if variant.get('work_cost'):
                        c = check_work_accounting(text, spec['vertices'])
                        row['counters'] = c
                        row['comm'] = fields(text, 'COMM_SHARE')
                        clock = fields(text, 'WORK_CLOCK')
                        row['queue'] = {}
                        for kind in ['push', 'pop']:
                            if c[kind+'_samples']:
                                mean = c[kind+'_ticks']/c[kind+'_samples']
                                row['queue'][kind] = dict(ns=mean*clock['window_us']*1000/clock['window_ticks'],
                                    estimated_share=mean*c[kind+'_calls']/clock['window_ticks'])
                row['valid'] = True
            except Exception as error:
                row['error'] = str(error)
            records.append(row)
            with (out/'runs.jsonl').open('a') as f:
                f.write(json.dumps(row)+'\n')
            print(label, 'PASS' if row['valid'] else row.get('error'), row.get('seconds'), flush=True)
            if not row['valid']:
                raise RuntimeError(f'Invalid solve retained in {log}: {row.get("error")}')
            return row

        # Warm each ACIC arm on a training source before investing in baseline tuning.
        for variant in variants:
            launch(variant, train[0], 'acic-smoke', -1)
        wasp = dict(label='wasp', binary='wasp_sssp')
        grid = [dict(threads=t, delta=d) for t,d in
                itertools.product(protocol['wasp_threads'], spec['wasp_deltas'])]
        launch(wasp, train[0], 'tune-warmup', -1, grid[len(grid)//2])
        order = list(itertools.product(grid, train)); rng.shuffle(order)
        for setting, ref in order:
            launch(wasp, ref, 'tune', setting=setting)

        def score(setting):
            return statistics.geometric_mean(r['seconds'] for r in records
                if r['graph']==graph and r['phase'] in ['tune','confirm'] and r['setting']==setting)

        finalists = sorted(grid, key=score)[:2]
        order = list(itertools.product(finalists, train)); rng.shuffle(order)
        for setting, ref in order:
            launch(wasp, ref, 'confirm', 1, setting)
        winner = min(finalists, key=score)
        selection = dict(winner=winner, scores=[dict(**s, geomean_seconds=score(s)) for s in grid])
        (out/(graph+'-wasp-selected.json')).write_text(json.dumps(selection, indent=2)+'\n')
        print('FROZEN WASP', graph, winner, flush=True)
        arms = variants+[wasp]
        for rep in range(-1, 3):
            sources = test[:]; rng.shuffle(sources)
            for ref in sources:
                order = arms[:]; rng.shuffle(order)
                for variant in order:
                    launch(variant, ref, 'warmup' if rep<0 else 'timing', rep,
                           winner if variant['label']=='wasp' else None)
        for variant in spec['work_variants']:
            for ref in test[:2]:
                launch(variant, ref, 'work')
        graph_rows = [r for r in records if r['graph']==graph]
        expected = 4+1+len(grid)*2+4+len(arms)*4*4+len(spec['work_variants'])*2
        assert len(graph_rows)==expected and all(r['valid'] for r in graph_rows)
        result = dict(validated_solves=len(graph_rows), wasp_selection=selection,
                      expected_process_sharing=spec['expected_process_sharing'], sources=[],
                      work=[r for r in graph_rows if r['phase']=='work'])
        for ref in test:
            values = {}
            for arm in arms:
                rows = [r for r in graph_rows if r['source']==ref[0] and
                        r['label']==arm['label'] and r['phase']=='timing']
                assert len(rows)==3
                values[arm['label']] = dict(median=statistics.median(r['seconds'] for r in rows),
                    seconds=[r['seconds'] for r in rows], scans=[r.get('scan_factor') for r in rows],
                    rounds=[r.get('rounds') for r in rows])
            for value in values.values():
                value['speedup_over_original'] = values['base']['median']/value['median']
                value['speedup_over_wasp'] = values['wasp']['median']/value['median']
            result['sources'].append(dict(source=ref[0], variants=values))
        summaries[graph] = result
        (out/'summary.json').write_text(json.dumps(dict(manifest=manifest, graphs=summaries), indent=2)+'\n')
        print('GRAPH COMPLETE', graph, json.dumps(result['sources']), flush=True)
    print('COMPLETE', out, 'validated', len(records), flush=True)


if __name__=='__main__':
    main()
