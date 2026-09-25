#!/usr/bin/env python3
"""Select road layout using training sources, then compare held-out with Wasp."""
import argparse
from road_experiment import Study


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign')
    args=ap.parse_args()
    s=Study(args.campaign,'layout','layout.json')
    candidates=[dict(label=l['label'],binary='acic_base',layout=l,flags=['--heap-slice','8'])
                for l in s.protocol['layouts']]
    base=candidates[0]
    s.block(candidates,s.train,'train-warmup',-1)
    for rep in range(2): s.block(candidates,s.train,'train',rep)
    top=sorted(candidates,key=lambda a:s.score(a,'train'))[:2]
    finalists=top if base in top else top+[base]
    for rep in range(3): s.block(finalists,s.train,'confirm',rep)
    winner=min(finalists,key=lambda a:s.score(a,'confirm'))
    selection=dict(winner=winner,screen_scores={a['label']:s.score(a,'train') for a in candidates},
                   confirmation_scores={a['label']:s.score(a,'confirm') for a in finalists})
    s.write('selection.json',selection)
    print('FROZEN LAYOUT',winner,flush=True)
    arms=[dict(base,label='base'),dict(winner,label='selected'),dict(base,label='control'),
          dict(label='wasp',binary='wasp_sssp',**s.protocol['wasp'])]
    for rep in range(-1,3): s.block(arms,s.test,'warmup' if rep<0 else 'timing',rep)
    work=[dict(a,label=a['label']+'_cost',binary='acic_base_cost',work_cost=True) for a in arms[:2]]
    s.block(work,s.test[:2],'work',0)
    s.finish(arms,selection,len(candidates)*6+len(finalists)*6+64+4)


if __name__=='__main__': main()
