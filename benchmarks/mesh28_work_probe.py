#!/usr/bin/env python3
"""Full-graph work conservation and sampled queue costs for selected variants."""
import argparse,json,os,re,subprocess
from pathlib import Path
from check_onenode_digest import check
from work_cost_report import check_work_accounting
from onenode_report import fields
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('campaign',type=Path);ap.add_argument('variants',type=Path);a=ap.parse_args()
root=a.campaign;app=Path(__file__).resolve().parents[1];out=root/'logs'/('work-'+os.environ['SLURM_JOB_ID']);out.mkdir()
ref=root/'graphs/mesh28-z.reference.txt';sources=[l.split()[0] for l in ref.read_text().splitlines() if l[:1].isdigit() and l.split()[1]=='test'][:2]
records=[]
for v in json.loads(a.variants.read_text()):
 for source in sources:
  log=out/(v['label']+'-'+source+'.out')
  cmd=['srun','-N1','-n16','--ntasks-per-node=16','-c8','--cpu-bind=none','--unbuffered','--kill-on-bad-exit=1','--time=6','bash',str(app/'benchmarks/launch_acic.sh'),'16','112',str(root/'bin'/v['binary']),'0',str(root/'graphs/mesh28-z.wsg'),'1',source,'4','0.999','0.005','--result-digest','--timeout','60']+v['flags']
  with log.open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
  check(ref,source,log);txt=log.read_text();c=check_work_accounting(txt,268435456);clock=fields(txt,'WORK_CLOCK')
  queue={}
  for k in ['push','pop']:
   mean=c[k+'_ticks']/c[k+'_samples'];queue[k]=dict(cycles=mean,ns=mean*clock['window_us']*1000/clock['window_ticks'],estimated_share=mean*c[k+'_calls']/clock['window_ticks'])
  records.append(dict(label=v['label'],source=source,seconds=float(re.search(r'Compute time: ([0-9.eE+-]+)',txt)[1]),counters=c,queue=queue,comm=fields(txt,'COMM_SHARE'),command=cmd,valid=True,log=str(log)))
  (out/'runs.json').write_text(json.dumps(records,indent=2)+'\n');print(v['label'],source,'PASS',c['edge_attempts'],flush=True)
