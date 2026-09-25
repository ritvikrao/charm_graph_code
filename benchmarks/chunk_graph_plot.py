#!/usr/bin/env python3
"""Plot the audited road/uniform comparison; error bars retain all repetitions."""
import argparse
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('audit', type=Path)
ap.add_argument('output', type=Path)
ap.add_argument('--final-baseline', action='store_true', help='use matched boundary-job uniform times')
a = ap.parse_args()
data = json.loads(a.audit.read_text())
if a.final_baseline:
    boundary = data['uniform_wasp_boundary']
    assert boundary['sources']
    data['graphs']['uniform25'] = dict(expected_process_sharing='off',
        wasp_selection=dict(winner=boundary['winner']), comparison_job=boundary['manifest']['job'],
        sources=[dict(source=r['source'], variants={key:dict(median=value, seconds=r['seconds'][key])
                  for key,value in r['medians'].items()}) for r in boundary['sources']])
fig, axes = plt.subplots(1, len(data['graphs']), figsize=(13, 5.5), constrained_layout=True)
variants = [('base', 'Original ACIC', '#8b98a5'),
            ('chunks8', 'Chunks / slice 8', '#64a5cf'),
            ('chunks64', 'Chunks / slice 64', '#156795'),
            ('wasp', 'Tuned Wasp', '#d69c35')]
for ax, (graph, result) in zip(np.atleast_1d(axes), data['graphs'].items()):
    rows = result['sources']
    x = np.arange(len(rows)); width = .19
    active = [v for v in variants if v[0] in rows[0]['variants']]
    for i, (key, label, color) in enumerate(active):
        med = [r['variants'][key]['median'] for r in rows]
        raw = [r['variants'][key]['seconds'] for r in rows]
        err = [[m-min(v) for m,v in zip(med,raw)], [max(v)-m for m,v in zip(med,raw)]]
        ax.bar(x+(i-(len(active)-1)/2)*width, med, width, label=label, color=color, yerr=err, capsize=2)
    winner = result['wasp_selection']['winner']
    ax.set_title(f"{graph}\nProcess sharing {result['expected_process_sharing']}; "
                 f"Wasp {winner['threads']} threads, Δ {winner['delta']}\n"
                 f"Job {result.get('comparison_job',data['manifest']['job'])}", fontsize=11)
    ax.set_xticks(x, [r['source'] for r in rows], rotation=20, fontsize=9)
    ax.set_xlabel('Held-out source vertex')
    ax.set_ylabel('Solve time (seconds; lower is better)')
    ax.spines[['top','right']].set_visible(False)
    ax.set_axisbelow(True); ax.yaxis.grid(True, alpha=.2)
    ax.set_ylim(bottom=0)
handles, labels = np.atleast_1d(axes)[0].get_legend_handles_labels()
fig.legend(handles, labels, loc='outside upper center', ncol=4, frameon=False)
fig.supxlabel('One exclusive Delta node; ACIC 16 × 7, +old-scheduler. Medians and min–max of 3 solves/source.\n'
              'Arms interleaved; Wasp selected on training sources. Each panel uses matched times within its job.', fontsize=9)
a.output.parent.mkdir(parents=True, exist_ok=True)
for ext in ['png','pdf']: fig.savefig(a.output.with_suffix('.'+ext), dpi=180)
