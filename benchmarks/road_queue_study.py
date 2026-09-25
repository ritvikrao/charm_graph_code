#!/usr/bin/env python3
"""Road queue 2x2 ablation, followed by a frozen layout/queue combination."""
import argparse
import json
import statistics
from pathlib import Path
from road_experiment import Study


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign')
    args=ap.parse_args()
    s=Study(args.campaign,'queue','queue.json')
    candidates=s.protocol['candidates']
    base=candidates[0]
    control=dict(base,label='train_control')
    training=candidates+[control]
    s.block(training,s.train,'train-warmup',-1)
    for rep in range(3): s.block(training,s.train,'train',rep)
    scores={a['label']:s.score(a,'train') for a in training}
    # Only the training sources participate in the choice. The edge-work
    # guard is also training-only and defined before any timing is observed.
    eligible=[]
    diagnostics={}
    for arm in candidates:
        ratios=[]
        work_ratios=[]
        for source in s.train:
            rows=[r for r in s.rows if r['source']==source and r['phase']=='train']
            med=lambda label,key: statistics.median(r[key] for r in rows if r['label']==label)
            ratios.append(med(base['label'],'seconds')/med(arm['label'],'seconds'))
            work_ratios.append(med(arm['label'],'edge_attempts')/med(base['label'],'edge_attempts'))
        diagnostics[arm['label']]=dict(per_source_speedups=ratios,edge_work_ratios=work_ratios)
        if (arm==base or (min(ratios)>=1.05 and max(work_ratios)<=1.25 and
                         scores[arm['label']]<min(scores[base['label']],scores[control['label']])/1.05)):
            eligible.append(arm)
    winner=min(eligible,key=lambda a:scores[a['label']])
    layout_result=json.loads(Path(s.protocol['layout_summary']).read_text())
    assert layout_result['validated_solves']>0
    layout=layout_result['selection']['winner']['layout']
    selection=dict(winner=winner,scores=scores,diagnostics=diagnostics,
                   layout=layout,layout_summary=s.protocol['layout_summary'])
    s.write('selection.json',selection)
    print('FROZEN QUEUE AND LAYOUT',winner,layout,flush=True)
    arms=[dict(base,label='base'),dict(winner,label='queue'),
          dict(base,label='layout',layout=layout),dict(winner,label='combined',layout=layout),
          dict(base,label='control'),dict(label='wasp',binary='wasp_sssp',**s.protocol['wasp'])]
    for rep in range(-1,3): s.block(arms,s.test,'warmup' if rep<0 else 'timing',rep)
    work=[dict(a,label=a['label']+'_cost',binary=a['cost_binary'],work_cost=True) for a in candidates]
    s.block(work,s.train,'work',0)
    # Match diagnostic evidence to the combined profile if placement changes.
    extra=[] if layout==base['layout'] else [dict(winner,label='combined_cost',layout=layout,
                                                  binary=winner['cost_binary'],work_cost=True)]
    s.block(extra,s.train,'work',0)
    s.finish(arms,selection,len(training)*8+96+len(work)*2+len(extra)*2)


if __name__=='__main__': main()
