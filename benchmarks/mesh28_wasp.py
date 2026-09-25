#!/usr/bin/env python3
"""Single-node matched Wasp/ACIC comparison; separate work and Projections runs."""
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
import time
from check_onenode_digest import check
from work_cost_report import production_attempts, check_work_accounting
from onenode_report import fields


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign', type=Path)
    args = ap.parse_args()
    root = args.campaign.resolve()
    app = Path(__file__).resolve().parents[1]
    cfg = json.loads((root/'protocol/protocol.json').read_text())
    assert int(os.environ['SLURM_NNODES']) == 1
    out = root/'logs'/('compare-'+os.environ['SLURM_JOB_ID'])
    out.mkdir()
    refs = [line.split() for line in (root/'graphs/mesh28-z.reference.txt').read_text().splitlines() if line[:1].isdigit()]
    train, test = refs[:2], refs[2:6]
    assert len(test) == 4 and all(r[1]=='test' for r in test)
    reference = root/'graphs/mesh28-z.reference.txt'
    hashes = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'bin').iterdir() if p.is_file() and p.suffix != '.manifest'}
    manifest = dict(protocol=cfg, job=os.environ['SLURM_JOB_ID'], hosts=os.environ['SLURM_JOB_NODELIST'], binaries=hashes, references=refs, environment={k:os.environ.get(k) for k in ['SLURM_MPI_TYPE','FI_CXI_RX_MATCH_MODE','OMP_PLACES','OMP_PROC_BIND']})
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    subprocess.run(['lscpu'],stdout=(out/'lscpu.txt').open('w'),check=True)
    subprocess.run(['numactl','--hardware'],stdout=(out/'numa.txt').open('w'),check=True)
    records=[]

    def launch(engine, ref, phase, rep=0, setting=None, trace=None):
        index=len(records)
        source=ref[0]
        env=dict(os.environ,OMP_DYNAMIC='FALSE',OMP_PLACES='cores',OMP_PROC_BIND='close')
        label=f'{index:03d}-{phase}-{engine}-s{source}-r{rep}'
        log=out/(label+'.out')
        srun=['srun','-N1','--cpu-bind=none','--unbuffered','--kill-on-bad-exit=1','--time=00:10:00']
        if engine.startswith('acic'):
            cmd=srun+['-n16','--ntasks-per-node=16','-c8','bash',str(app/'benchmarks/launch_acic.sh'),'16','112',str(root/'bin'/engine),'0',str(root/'graphs/mesh28-z.wsg'),'1',source,'4','0.999','0.005','--result-digest','--timeout','300']+cfg['acic']['flags']
            if trace:
                trace.mkdir(parents=True)
                cmd+=['+traceoff','+logsize','8000000','+traceroot',str(trace)]
        else:
            env['OMP_NUM_THREADS']=str(setting['threads'])
            cmd=srun+['-n1','-c128',str(root/'bin'/engine),str(root/'graphs/mesh28-z.wsg'),source,str(setting['delta'])]
        start=time.monotonic()
        with log.open('w') as stream:
            result=subprocess.run(cmd,stdout=stream,stderr=subprocess.STDOUT,env=env)
        row=dict(index=index,engine=engine,phase=phase,source=int(source),role=ref[1],rep=rep,setting=setting,command=cmd,log=str(log),returncode=result.returncode,launch_seconds=time.monotonic()-start,valid=False)
        txt=log.read_text()
        try:
            if result.returncode: raise ValueError('nonzero launch exit')
            check(reference,source,log)
            pattern=r'Compute time: ([0-9.eE+-]+)' if engine.startswith('acic') else r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)'
            times=re.findall(pattern,txt)
            if len(times)!=1 or not math.isfinite(float(times[0])) or float(times[0])<=0: raise ValueError('missing/invalid solve time')
            row['seconds']=float(times[0])
            if engine.startswith('acic'):
                row['edge_attempts']=production_attempts(txt,cfg['vertices'])
                row['edge_scan_factor']=row['edge_attempts']/cfg['directed_edges']
                if engine=='acic_cost': row['counters']=check_work_accounting(txt,cfg['vertices'])
                row['rounds']=len(re.findall(r'Updates: created:',txt))
                if engine=='acic_cost': row['comm_share']=fields(txt,'COMM_SHARE')
            if engine=='wasp_cost':
                row['edge_inspections']=int(re.search(r'Number of relaxations: (\d+)',txt)[1])
            if trace and 'Projections log flushed to disk' in txt: raise ValueError('trace buffer flushed during solve')
            row['valid']=True
        except Exception as e:
            row['error']=str(e)
        records.append(row)
        with (out/'runs.jsonl').open('a') as stream: stream.write(json.dumps(row)+'\n')
        print(label, 'VALID' if row['valid'] else row.get('error'),row.get('seconds'),flush=True)
        if not row['valid']: raise RuntimeError(str(row))
        return row

    rng=random.Random(20260925)
    settings=[dict(threads=t,delta=d) for t,d in itertools.product(cfg['wasp']['threads'],cfg['wasp']['delta'])]
    launch('wasp_sssp',train[0],'tune-warmup',setting=dict(threads=128,delta=4096))
    order=list(itertools.product(settings,train)); rng.shuffle(order)
    for setting,ref in order: launch('wasp_sssp',ref,'tune',setting=setting)
    def score(s):
        return statistics.geometric_mean(r['seconds'] for r in records if r['phase'] in ['tune','confirm'] and r['setting']==s)
    finalists=sorted(settings,key=score)[:2]
    order=list(itertools.product(finalists,train)); rng.shuffle(order)
    for setting,ref in order: launch('wasp_sssp',ref,'confirm',1,setting=setting)
    winner=min(finalists,key=score)
    (out/'wasp-selected.json').write_text(json.dumps(dict(winner=winner,scores=[dict(**s,geomean_seconds=score(s)) for s in settings]),indent=2)+'\n')
    print('FROZEN WASP',winner,flush=True)
    for ref in test:
        launch('acic',ref,'warmup')
        launch('wasp_sssp',ref,'warmup',setting=winner)
    for rep in range(3):
        order=test[:]; rng.shuffle(order)
        for ref in order:
            engines=['acic','wasp_sssp']; rng.shuffle(engines)
            for engine in engines: launch(engine,ref,'timing',rep,setting=winner if engine=='wasp_sssp' else None)
    for ref in test[:2]:
        launch('acic_cost',ref,'work')
        launch('wasp_cost',ref,'work',setting=winner)
    launch('acic',test[0],'trace-control-before')
    trace=root/'traces'/('mesh28-z-'+os.environ['SLURM_JOB_ID'])
    launch('acic_prj',test[0],'trace',trace=trace)
    launch('acic',test[0],'trace-control-after')
    for name,extra in [('projections_report',['--pes-per-process','7','--processes-per-node','16','--bin-ms','100']),('heap_backlog_report',[])]:
        with (out/(name+'.txt')).open('w') as stream:
            subprocess.run(['python3',str(app/'benchmarks'/(name+'.py')),str(trace/'acic_prj'),'--jobs','16','--json',str(out/(name+'.json'))]+extra,stdout=stream,stderr=subprocess.STDOUT,check=True)
    print('COMPLETE',out,'TRACE',trace,flush=True)


if __name__=='__main__': main()
