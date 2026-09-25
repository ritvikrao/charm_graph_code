#!/usr/bin/env python3
"""Standalone Delta road layout/queue figures from audited raw-run summaries."""
import argparse
import json
from pathlib import Path
import statistics
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('audit',type=Path)
    ap.add_argument('output',type=Path)
    a=ap.parse_args()
    data=json.loads(a.audit.read_text())
    queue='candidates' in data['manifest']['protocol']
    time_first=data['manifest']['protocol'].get('time_first',False)
    scores={label:statistics.geometric_mean(r['seconds'] for r in values)
            for label,values in data['training'].items()}
    labels=sorted(scores,key=scores.get)
    fig,(ax,bx)=plt.subplots(1,2,figsize=(14,7),gridspec_kw={'width_ratios':[1.05,1.3]})
    colors=['#cf6c32' if ('span64' in l or 'span32' in l) else '#437ba8' for l in labels]
    bars=ax.barh(labels,[scores[l] for l in labels],color=colors)
    ax.invert_yaxis()
    ax.bar_label(bars,fmt='%.3f',padding=3,fontsize=9)
    ax.set_xlim(0,max(scores.values())*1.19)
    ax.set_xlabel('Solve seconds (geomean of two training-source medians)')
    ax.set_title('Training screen: '+('wide queue at different layouts' if time_first else
                 'queue / distance-band ablation' if queue else 'processes × workers per process / placement'))
    variants=(['base','queue','combined','control','wasp'] if time_first else
              ['base','queue','layout','combined','control','wasp'] if queue else
              ['base','selected','control','wasp'])
    names={'base':'Original 16×7','queue':'Selected queue, 16×7',
           'layout':'Original, selected layout','combined':'Selected queue + layout',
           'selected':'Selected layout','control':'Original 16×7 control','wasp':'Wasp 64 threads'}
    x=np.arange(len(data['sources']))
    width=.8/len(variants)
    palette=(['#536d8a','#328da0','#db9447','#b6bec7','#8f639e'] if time_first else
             ['#536d8a','#328da0','#749e57','#db9447','#b6bec7','#8f639e'] if queue else
             ['#536d8a','#328da0','#b6bec7','#8f639e'])
    for i,label in enumerate(variants):
        vals=[source['variants'][label] for source in data['sources']]
        m=np.array([v['median'] for v in vals])
        low=m-np.array([min(v['seconds']) for v in vals])
        high=np.array([max(v['seconds']) for v in vals])-m
        bx.bar(x+(i-(len(variants)-1)/2)*width,m,width,label=names[label],color=palette[i],
               yerr=np.array([low,high]),capsize=2,error_kw={'linewidth':.8})
    bx.set_xticks(x,[source['source'] for source in data['sources']],rotation=20)
    bx.set_xlabel('Test source (not used for selection)')
    bx.set_ylabel('Solve seconds; median with min–max across three launches')
    bx.set_title('Fresh matched confirmation; lower is faster')
    bx.legend(fontsize=8,loc='upper right')
    if len(variants)>4:
        bx.set_ylim(top=max(max(v['seconds']) for source in data['sources']
                           for v in source['variants'].values())*1.5)
    for axis in (ax,bx):
        axis.spines[['top','right']].set_visible(False)
    selected=data['selection']['winner']['label']
    description=(f'Full chunks / band 65536 / slice 64; selected layout {selected}' if time_first else
                 f"Training-selected {'queue' if queue else 'layout'}: {selected}")
    fig.suptitle(f"Delta road-usa-z · job {data['manifest']['job']} · {data['manifest']['hosts']}\n{description}",fontsize=14)
    fig.text(.5,.015,'One exclusive node; +old-scheduler; every solve and worker affinity validated. Production timings only.',ha='center',fontsize=10)
    if time_first:
        fig.text(.5,.04,'16×7 retained: 8×14 missed the ≥5% benefit requirement on both training sources; the two wide-queue arms share the same layout.',ha='center',fontsize=9)
    if queue and data['selection']['winner']['label']=='heap8':
        fig.text(.5,.04,'All held-out ACIC arms selected the original heap; the faster wide-band training candidate needs separate confirmation.',ha='center',fontsize=9)
    fig.tight_layout(rect=(0,.065,1,.92))
    a.output.parent.mkdir(parents=True,exist_ok=True)
    for ext in ('.png','.pdf'): fig.savefig(a.output.with_suffix(ext),dpi=160)


if __name__=='__main__': main()
