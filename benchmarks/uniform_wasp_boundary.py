#!/usr/bin/env python3
"""Check Wasp's lower-delta boundary using training sources, then retime if needed."""
import argparse
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
from check_onenode_digest import check
from work_cost_report import production_attempts

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('campaign', type=Path)
ap.add_argument('prior_job')
a = ap.parse_args()
root = a.campaign.resolve(); app = Path(__file__).resolve().parents[1]
assert int(os.environ['SLURM_NNODES'])==1
prior = root/'logs'/('compare-'+a.prior_job)
selection = json.loads((prior/'uniform25-wasp-selected.json').read_text())['winner']
assert selection['delta']==16
protocol = json.loads((root/'protocol/protocol.json').read_text())
spec = next(s for s in protocol['graphs'] if s['name']=='uniform25')
reference = root/'graphs/uniform25.reference.txt'
refs = [s.split() for s in reference.read_text().splitlines() if s[:1].isdigit()]
train = [r for r in refs if r[1]=='tune'][:2]; test = [r for r in refs if r[1]=='test'][:4]
assert len(train)==2 and len(test)==4
out = root/'logs'/('wasp-boundary-'+os.environ['SLURM_JOB_ID']);out.mkdir()
manifest = dict(job=os.environ['SLURM_JOB_ID'],hosts=os.environ['SLURM_JOB_NODELIST'],prior_job=a.prior_job,
                prior_selection=selection,threads=selection['threads'],
                method='Pin training-selected Wasp threads; compare delta 16 vs 4 twice on each training source. If 4 wins, compare 4 vs 1 twice; stop there. If selection changes, retime original ACIC, selected chunks/s64 and Wasp on four held-out sources with warmup plus three repetitions. All arms interleaved. No solver changes or delta selection on held-out sources.')
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
rows=[];rng=random.Random(20260927)
variants = {v['label']:v for v in spec['variants']}

def launch(label, ref, phase, rep, delta=None):
    source=ref[0];log=out/f'{len(rows):03d}-{phase}-{label}-s{source}-r{rep}-d{delta}.out'
    env=dict(os.environ,OMP_DYNAMIC='FALSE',OMP_PLACES='cores',OMP_PROC_BIND='close',OMP_NUM_THREADS=str(selection['threads']))
    cmd=['srun','-N1','--cpu-bind=none','--unbuffered','--kill-on-bad-exit=1','--time=5']
    if label=='wasp':
        cmd+=['-n1','-c128',str(root/'bin/wasp_sssp'),str(root/'graphs/uniform25.wsg'),source,str(delta)]
    else:
        v=variants[label]
        cmd+=['-n16','--ntasks-per-node=16','-c8','bash',str(app/'benchmarks/launch_acic.sh'),'16','112',str(root/'bin'/v['binary']),'0',str(root/'graphs/uniform25.wsg'),'1',source,'4','0.999','0.005','--result-digest','--timeout','60']+v['flags']
    row=dict(label=label,source=source,role=ref[1],phase=phase,rep=rep,delta=delta,command=cmd,log=str(log),valid=False)
    try:
        with log.open('w') as f:rc=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,env=env,timeout=360).returncode
        row['returncode']=rc
        assert rc==0
        check(reference,source,log);text=log.read_text()
        pattern=r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)' if label=='wasp' else r'Compute time: ([0-9.eE+-]+)'
        times=re.findall(pattern,text);assert len(times)==1
        row['seconds']=float(times[0]);assert math.isfinite(row['seconds']) and row['seconds']>0
        if label!='wasp':
            assert 'Process sharing: off' in text and 'Using the original scheduler (+old-scheduler)' in text
            row['edge_attempts']=production_attempts(text,spec['vertices'])
            row['rounds']=text.count('Updates: created:')
        row['valid']=True
    except Exception as error:row['error']=str(error)
    rows.append(row)
    with (out/'runs.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
    print(log.name,'PASS' if row['valid'] else row.get('error'),row.get('seconds'),flush=True)
    if not row['valid']:raise RuntimeError(str(row))
    return row

launch('wasp',train[0],'warmup-training',-1,16)
scores={}
for phase,deltas in [('boundary',[16,4]),('lower-neighbor',[4,1])]:
    if phase=='lower-neighbor' and scores[16]<=scores[4]:break
    stage=[]
    for rep in range(2):
        order=list((d,r) for d in deltas for r in train);rng.shuffle(order)
        for delta,ref in order:stage.append(launch('wasp',ref,phase,rep,delta))
    for delta in deltas:
        scores[delta]=statistics.geometric_mean(r['seconds'] for r in stage if r['delta']==delta)
winner=min(scores,key=scores.get)
result=dict(manifest=manifest,training_scores=scores,winner=dict(threads=selection['threads'],delta=winner),sources=[])
(out/'selection.json').write_text(json.dumps(result,indent=2)+'\n')
print('FROZEN WASP',result['winner'],flush=True)
if winner!=16:
    for rep in range(-1,3):
        order=list((label,ref) for label in ['base','chunks64','wasp'] for ref in test);rng.shuffle(order)
        for label,ref in order:
            launch(label,ref,'warmup-heldout' if rep<0 else 'timing',rep,winner if label=='wasp' else None)
    for ref in test:
        values={}
        for label in ['base','chunks64','wasp']:
            values[label]=[r['seconds'] for r in rows if r['phase']=='timing' and r['label']==label and r['source']==ref[0]]
            assert len(values[label])==3
        result['sources'].append(dict(source=ref[0],seconds=values,medians={k:statistics.median(v) for k,v in values.items()}))
result['validated_solves']=len(rows)
(out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print('COMPLETE',out,'validated',len(rows),flush=True)
