#!/usr/bin/env python3
"""Recheck the previously training-selected Wasp baseline in this allocation."""
import argparse,json,os,re,subprocess
from pathlib import Path
from check_onenode_digest import check
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('campaign',type=Path);a=ap.parse_args();root=a.campaign
out=root/'logs'/('wasp-fixed-'+os.environ['SLURM_JOB_ID']);out.mkdir();ref=root/'graphs/mesh28-z.reference.txt'
sources=[l.split()[0] for l in ref.read_text().splitlines() if l[:1].isdigit() and l.split()[1]=='test'];rows=[]
env=dict(os.environ,OMP_NUM_THREADS='128',OMP_PLACES='cores',OMP_PROC_BIND='close',OMP_DYNAMIC='FALSE')
for rep in range(-1,3):
 for source in sources:
  log=out/(source+'-r'+str(rep)+'.out');cmd=['srun','-N1','-n1','-c128','--cpu-bind=none','--unbuffered','--kill-on-bad-exit=1','--time=6',str(root/'bin/wasp_sssp'),str(root/'graphs/mesh28-z.wsg'),source,'4096']
  with log.open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,env=env,check=True)
  check(ref,source,log);seconds=float(re.search(r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)',log.read_text())[1]);rows.append(dict(source=source,rep=rep,seconds=seconds,valid=True,command=cmd,log=str(log)))
  (out/'runs.json').write_text(json.dumps(rows,indent=2)+'\n');print('WASP',source,rep,seconds,flush=True)
