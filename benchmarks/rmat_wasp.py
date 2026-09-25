#!/usr/bin/env python3
"""One-node RMAT comparison with lazy-path-aware validation and Wasp tuning."""
import argparse
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
from road_experiment import sha256
from road_layout_launch import core_maps


def validate(reference, row):
    check(reference, row['source'], row['log'])
    text=Path(row['log']).read_text()
    wasp=row['binary']=='wasp_sssp'
    pattern=r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)' if wasp else r'Compute time: ([0-9.eE+-]+)'
    values=re.findall(pattern,text)
    assert len(values)==1 and math.isfinite(float(values[0])) and float(values[0])>0
    result=dict(seconds=float(values[0]))
    if wasp: return result
    assert 'Starting Reconverse with 16 processes, 112 PEs' in text
    assert 'Using the original scheduler (+old-scheduler)' in text
    assert re.search(r'^Process sharing: off$',text,re.M)
    assert re.search(r'^Heap slice: '+str(row['slice'])+r' \(inactive\)$',text,re.M)
    assert 'Hub hints: degree >= 256 (auto)' in text
    tokens=re.findall(r'Lazy heavy: (\d+) tokens, (\d+) stale,',text)
    assert len(tokens)==1 and int(tokens[0][0])>0
    pins=[tuple(map(int,v)) for v in re.findall(r'set PE (\d+) on node (\d+) to PU P#(\d+)',text)]
    expected=[(rank*7+pe,rank,cpu) for rank,cpus in enumerate(core_maps(row['layout']))
              for pe,cpu in enumerate(cpus)]
    assert sorted(pins)==expected
    hints=re.findall(r'Hub hints: published (\d+), filtered (\d+),',text)
    assert len(hints)==1
    result.update(rounds=text.count('Updates: created:'),lazy_tokens=int(tokens[0][0]),
                  stale_tokens=int(tokens[0][1]),hints_published=int(hints[0][0]),
                  hints_filtered=int(hints[0][1]),affinity_verified=True,
                  process_sharing='off',lazy_heavy='on',hub_hint_degree=256)
    # Lazy tokens participate in retirement accounting. The road/non-lazy
    # ledger formula is deliberately not used to infer RMAT edge attempts.
    return result


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign',type=Path)
    args=ap.parse_args()
    root=args.campaign.resolve();app=Path(__file__).resolve().parent
    cfg=json.loads((root/'protocol/protocol.json').read_text())
    assert int(os.environ['SLURM_NNODES'])==1
    out=root/'logs'/('compare-'+os.environ['SLURM_JOB_ID']);out.mkdir()
    graph=cfg['graph'];reference=root/'graphs'/(graph+'.reference.txt')
    refs=[x.split() for x in reference.read_text().splitlines() if x[:1].isdigit()]
    train=[r[0] for r in refs if r[1]=='tune'][:2]
    test=[r[0] for r in refs if r[1]=='test'][:4]
    assert len(train)==2 and len(test)==4
    runtime=json.loads((root/'protocol/runtime-manifest.json').read_text())
    for path,digest in runtime['libraries'].items(): assert sha256(path)==digest
    manifest=dict(job=os.environ['SLURM_JOB_ID'],hosts=os.environ['SLURM_JOB_NODELIST'],
        protocol=cfg,runtime=runtime,references=refs,
        binaries={p.name:sha256(p) for p in (root/'bin').iterdir() if not p.name.endswith('.manifest')},
        build_manifests={p.name:p.read_text() for p in (root/'bin').glob('*.manifest')},
        graph_hashes={suffix:sha256(root/'graphs'/(graph+suffix)) for suffix in ['.wsg','.meta','.reference.txt']},
        harness={name:sha256(app/name) for name in ['rmat_wasp.py','road_layout_launch.py','road_experiment.py','check_onenode_digest.py']})
    write=lambda name,data:(out/name).write_text(json.dumps(data,indent=2)+'\n')
    write('manifest.json',manifest)
    for name,cmd in [('lscpu.txt',['lscpu']),('numa.txt',['numactl','--hardware'])]:
        with (out/name).open('w') as f: subprocess.run(cmd,stdout=f,check=True)
    records=[];rng=random.Random(cfg['order_seed'])

    def launch(arm,source,phase,rep=0,setting=None):
        index=len(records)
        log=out/f'{index:03d}-{phase}-{arm["label"]}-s{source}-r{rep}.out'
        env=dict(os.environ,OMP_DYNAMIC='FALSE',OMP_PLACES='cores',OMP_PROC_BIND='close')
        cmd=['srun','-N1','--cpu-bind=none','--unbuffered','--kill-on-bad-exit=1','--time=5']
        if arm['binary']=='wasp_sssp':
            env['OMP_NUM_THREADS']=str(setting['threads'])
            cmd+=['-n1','-c128',str(root/'bin/wasp_sssp'),str(root/'graphs'/(graph+'.wsg')),source,str(setting['delta'])]
        else:
            cmd+=['-n16','--ntasks-per-node=16','-c8','python3',str(app/'road_layout_launch.py'),
                  json.dumps(arm['layout']),str(root/'bin'/arm['binary']),'0',str(root/'graphs'/(graph+'.wsg')),
                  '1',source,'4','0.999','0.005','--result-digest','--timeout','120']
            cmd+=cfg['common_flags']+['--heap-slice',str(arm['slice'])]
        row=dict(index=index,**arm,source=source,phase=phase,rep=rep,setting=setting,command=cmd,log=str(log),valid=False)
        try:
            with log.open('w') as f:
                row['returncode']=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,env=env,timeout=360).returncode
            assert row['returncode']==0,row['returncode']
            row.update(validate(reference,row));row['valid']=True
        except Exception as error: row['error']=repr(error)
        records.append(row)
        with (out/'runs.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
        print(index,phase,arm['label'],source,setting,row.get('seconds'),'PASS' if row['valid'] else row['error'],flush=True)
        if not row['valid']: raise RuntimeError(f'{log}: {row["error"]}')

    variants=cfg['variants']
    for arm in variants: launch(arm,train[0],'acic-smoke',-1)
    wasp=dict(label='wasp',binary='wasp_sssp')
    grid=[dict(threads=t,delta=d) for t,d in itertools.product(cfg['wasp_threads'],cfg['wasp_deltas'])]
    launch(wasp,train[0],'tune-warmup',-1,dict(threads=128,delta=16))
    cells=list(itertools.product(grid,train));rng.shuffle(cells)
    for setting,source in cells:launch(wasp,source,'tune',0,setting)
    def score(setting):
        return statistics.geometric_mean(statistics.median(r['seconds'] for r in records
            if r['source']==source and r['phase'] in ['tune','confirm'] and r['setting']==setting) for source in train)
    finalists=sorted(grid,key=score)[:2]
    cells=list(itertools.product(finalists,train));rng.shuffle(cells)
    for setting,source in cells:launch(wasp,source,'confirm',1,setting)
    winner=min(finalists,key=score)
    selection=dict(winner=winner,scores=[dict(**setting,geomean_seconds=score(setting)) for setting in grid],
                   boundary=dict(threads=winner['threads'] in [min(cfg['wasp_threads']),max(cfg['wasp_threads'])],
                                 delta=winner['delta'] in [min(cfg['wasp_deltas']),max(cfg['wasp_deltas'])]))
    write('wasp-selected.json',selection);print('FROZEN WASP',winner,flush=True)
    arms=variants+[wasp]
    for rep in range(-1,3):
        cells=list(itertools.product(arms,test));rng.shuffle(cells)
        for arm,source in cells:launch(arm,source,'warmup' if rep<0 else 'timing',rep,winner if arm==wasp else None)
    expected=len(variants)+1+len(grid)*2+4+len(arms)*4*4
    assert len(records)==expected and all(r['valid'] for r in records)
    sources=[]
    for source in test:
        values={}
        for arm in arms:
            rows=[r for r in records if r['phase']=='timing' and r['label']==arm['label'] and r['source']==source]
            assert len(rows)==3
            values[arm['label']]=dict(median=statistics.median(r['seconds'] for r in rows),seconds=[r['seconds'] for r in rows],
                                     rounds=[r.get('rounds') for r in rows])
        sources.append(dict(source=source,variants=values))
    write('summary.json',dict(manifest=manifest,validated_solves=len(records),selection=selection,sources=sources))
    print('COMPLETE',out,len(records),flush=True)


if __name__=='__main__':main()
