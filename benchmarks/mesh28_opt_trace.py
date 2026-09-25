#!/usr/bin/env python3
"""Matched Projections validation of the selected queue/slice combination."""
import argparse,json,os,re,subprocess
from pathlib import Path
from check_onenode_digest import check
from work_cost_report import production_attempts
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('campaign',type=Path);ap.add_argument('config',type=Path);a=ap.parse_args()
root=a.campaign;app=Path(__file__).resolve().parents[1];v=json.loads(a.config.read_text())
out=root/'traces'/('optimized-'+os.environ['SLURM_JOB_ID']);out.mkdir(parents=True)
ref=root/'graphs/mesh28-z.reference.txt';source=next(l.split()[0] for l in ref.read_text().splitlines() if l[:1].isdigit() and l.split()[1]=='test')
rows=[]
for label,binary in [('before',v['binary']),('trace',v['trace_binary']),('after',v['binary'])]:
 log=out/(label+'.out');cmd=['srun','-N1','-n16','--ntasks-per-node=16','-c8','--cpu-bind=none','--unbuffered','--kill-on-bad-exit=1','--time=6','bash',str(app/'benchmarks/launch_acic.sh'),'16','112',str(root/'bin'/binary),'0',str(root/'graphs/mesh28-z.wsg'),'1',source,'4','0.999','0.005','--result-digest','--timeout','60']+v['flags']
 if label=='trace':cmd+=['+traceoff','+logsize','8000000','+traceroot',str(out)]
 with log.open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
 check(ref,source,log);txt=log.read_text();assert 'Projections log flushed to disk' not in txt
 rows.append(dict(label=label,seconds=float(re.search(r'Compute time: ([0-9.eE+-]+)',txt)[1]),edge_attempts=production_attempts(txt,268435456),rounds=txt.count('Updates: created:'),command=cmd,valid=True,log=str(log)))
 (out/'runs.json').write_text(json.dumps(rows,indent=2)+'\n');print('TRACE',label,rows[-1]['seconds'],flush=True)
for name,extra in [('projections_report',['--pes-per-process','7','--processes-per-node','16','--bin-ms','50']),('heap_backlog_report',[])]:
 with (out/(name+'.txt')).open('w') as f:
  subprocess.run(['python3',str(app/'benchmarks'/(name+'.py')),str(out/v['trace_binary']),'--jobs','16','--json',str(out/(name+'.json'))]+extra,stdout=f,stderr=subprocess.STDOUT,check=True)
