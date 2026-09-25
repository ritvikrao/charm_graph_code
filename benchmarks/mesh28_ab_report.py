#!/usr/bin/env python3
"""Audit matched ACIC variants and retain time/work/control variation."""
import argparse,json,re,statistics
from pathlib import Path
from check_onenode_digest import check
from work_cost_report import production_attempts,check_work_accounting
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('directory',type=Path);a=ap.parse_args()
out=a.directory;root=out.parents[1];m=json.loads((out/'manifest.json').read_text());rows=[json.loads(l) for l in (out/'runs.jsonl').read_text().splitlines()]
assert len(rows)==len(m['variants'])*m['sources']*(m['reps']+1)
summary={};enriched=[]
for r in rows:
 p=out/f"{r['variant']}-s{r['source_index']}-r{r['rep']}.out";t=p.read_text();check(root/'graphs'/(r['graph']+'.reference.txt'),r['source'],p)
 r['edge_attempts']=production_attempts(t,268435456);r['scan_factor']=r['edge_attempts']/1073676288;r['rounds']=t.count('Updates: created:')
 if 'WORK_COST ' in t:r['counters']=check_work_accounting(t,268435456)
 enriched.append(r)
for s in range(m['sources']):
 vals={}
 for v in m['variants']:
  rs=[r for r in enriched if r['source_index']==s and r['variant']==v['label'] and r['rep']>=0]
  vals[v['label']]=dict(median=statistics.median(r['seconds'] for r in rs),seconds=[r['seconds'] for r in rs],scans=[r['scan_factor'] for r in rs],rounds=[r['rounds'] for r in rs])
 base=vals['base']['median']
 for d in vals.values():d['speedup_over_base']=base/d['median']
 summary[str(s)]=vals
result=dict(manifest=m,validated=len(rows),summary=summary,runs=enriched)
(out/'audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(summary,indent=2))
