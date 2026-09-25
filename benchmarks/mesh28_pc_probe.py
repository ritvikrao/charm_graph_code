#!/usr/bin/env python3
"""Solve-window CPU-timer samples bracketed by production timing controls."""
import argparse,json,os,re,subprocess
from pathlib import Path
from check_onenode_digest import check
from work_cost_report import production_attempts
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('campaign',type=Path);ap.add_argument('variants',type=Path)
a=ap.parse_args();root=a.campaign;app=Path(__file__).resolve().parents[1]
out=root/'profiles'/('pc-'+os.environ['SLURM_JOB_ID']);out.mkdir(parents=True)
variants=json.loads(a.variants.read_text());(out/'variants.json').write_text(json.dumps(variants,indent=2)+'\n')
ref=root/'graphs/mesh28-z.reference.txt'
source=next(l.split()[0] for l in ref.read_text().splitlines() if l[:1].isdigit() and l.split()[1]=='test')
records=[]
for v in variants:
  for i,kind in enumerate(['warmup','plain','sampled','off','sampled','plain']):
    label=v['label']+'-'+str(i)+'-'+kind;log=out/(label+'.out');prof=out/(label+'.prof')
    env=dict(os.environ);binary=v['binary']
    if kind in ['sampled','off']:
      binary=v['profile_binary'];prof.mkdir();env['ACIC_PROF_DIR']=str(prof)
      env['ACIC_PROF_TIMER']='250' if kind=='sampled' else ''
      if kind=='off':env.pop('ACIC_PROF_TIMER');env['ACIC_PROF_OFF']='1'
    cmd=['srun','-N1','-n16','--ntasks-per-node=16','-c8','--cpu-bind=none','--unbuffered','--kill-on-bad-exit=1','--time=6','bash',str(app/'benchmarks/launch_acic.sh'),'16','112',str(root/'bin'/binary),'0',str(root/'graphs/mesh28-z.wsg'),'1',source,'4','0.999','0.005','--result-digest','--timeout','300']+v['flags']
    with log.open('w') as stream:subprocess.run(cmd,stdout=stream,stderr=subprocess.STDOUT,env=env,check=True)
    check(ref,source,log);txt=log.read_text();seconds=float(re.search(r'Compute time: ([0-9.eE+-]+)',txt)[1])
    row=dict(label=label,variant=v['label'],kind=kind,source=source,seconds=seconds,edge_attempts=production_attempts(txt,268435456),command=cmd,log=str(log),valid=True)
    if kind in ['sampled','off']:
      files=list(prof.glob('pe*.txt'));assert len(files)==112
      row['samples']=sum(int(re.search(r'^samples (\d+)',p.read_text(),re.M)[1]) for p in files)
      row['dropped']=sum(int(re.search(r'^dropped (\d+)',p.read_text(),re.M)[1]) for p in files)
      if kind=='sampled':
        assert row['samples']>10000 and row['dropped']==0
        assert len(list(prof.glob('maps.*.txt')))==16
        assert 'timer_create failed' not in txt
        with (out/(label+'.md')).open('w') as stream:
          subprocess.run(['python3',str(app/'benchmarks/papi_profile_report.py'),str(prof),str(root/'bin'/binary),'--run-output',str(log),'--top','40','--json',str(out/(label+'.json'))],stdout=stream,stderr=subprocess.STDOUT,check=True)
    records.append(row);(out/'runs.json').write_text(json.dumps(records,indent=2)+'\n')
    print(label,seconds,row.get('samples'),flush=True)
print('PROFILE COMPLETE',out,flush=True)
