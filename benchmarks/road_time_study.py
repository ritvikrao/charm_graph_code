#!/usr/bin/env python3
"""Confirm a training-only time winner and recheck three layouts for that queue."""
import argparse
import statistics
from road_experiment import Study


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign')
    args=ap.parse_args()
    s=Study(args.campaign,'time','time.json')
    queue=s.protocol['queue_candidate']
    candidates=[dict(queue,label=l['label'],layout=l) for l in s.protocol['layouts']]
    s.block(candidates,s.train,'train-warmup',-1)
    for rep in range(3): s.block(candidates,s.train,'train',rep)
    baseline=candidates[0]
    eligible=[baseline]
    ratios={}
    for arm in candidates[1:]:
        ratio=[]
        for source in s.train:
            med=lambda label: statistics.median(r['seconds'] for r in s.rows
                if r['phase']=='train' and r['label']==label and r['source']==source)
            ratio.append(med(baseline['label'])/med(arm['label']))
        ratios[arm['label']]=ratio
        if min(ratio)>=1.05: eligible.append(arm)
    winner=min(eligible,key=lambda a:s.score(a,'train'))
    selection=dict(winner=winner,screen_scores={a['label']:s.score(a,'train') for a in candidates},
                   confirmation_scores={},layout_speedups=ratios,
                   queue_selection=s.protocol['queue_selection'])
    s.write('selection.json',selection)
    print('FROZEN TIME-FIRST PROFILE',winner,flush=True)
    base=s.protocol['baseline']
    arms=[dict(base,label='base'),dict(baseline,label='queue'),dict(winner,label='combined'),
          dict(base,label='control'),dict(label='wasp',binary='wasp_sssp',**s.protocol['wasp'])]
    for rep in range(-1,3): s.block(arms,s.test,'warmup' if rep<0 else 'timing',rep)
    work=[dict(a,label=a['label']+'_cost',binary=a['cost_binary'],work_cost=True) for a in arms[:3]]
    s.block(work,s.test[:2],'work',0)
    s.finish(arms,selection,len(candidates)*8+80+6)


if __name__=='__main__': main()
