#!/usr/bin/env python3
"""Reaudit road layout/queue outputs and show timing, rounds, and work."""
import argparse
import json
from pathlib import Path
import statistics
from road_experiment import validate, sha256
from chunk_graph_report import round_progress


def median(rows, label, source, key='seconds', phase='timing'):
    return statistics.median(r[key] for r in rows if r['label']==label and
                             r['source']==source and r['phase']==phase)


def audit(directory):
    out=Path(directory).resolve()
    root=out.parents[1]
    data=json.loads((out/'summary.json').read_text())
    rows=[json.loads(x) for x in (out/'runs.jsonl').read_text().splitlines()]
    protocol=data['manifest']['protocol']
    queue='candidates' in protocol
    if queue:
        extra=2 if data['selection']['layout']!=protocol['candidates'][0]['layout'] else 0
        expected=(len(protocol['candidates'])+1)*8+96+len(protocol['candidates'])*2+extra
    else:
        expected=len(protocol['layouts'])*6+len(data['selection']['confirmation_scores'])*6+64+4
    assert len(rows)==expected==data['validated_solves']
    seen=set()
    for row in rows:
        assert row['valid'] and row['returncode']==0
        key=(row['label'],row['source'],row['phase'],row['rep'])
        assert key not in seen
        seen.add(key)
        for k,v in validate(root/'graphs/road-usa-z.reference.txt',row).items(): assert row[k]==v
    for source in data['sources']:
        for label,v in source['variants'].items():
            raw=[r for r in rows if r['label']==label and r['source']==source['source'] and r['phase']=='timing']
            assert len(raw)==3 and [r['seconds'] for r in raw]==v['seconds']
            assert statistics.median(v['seconds'])==v['median']
    data['progress']=[dict(label=r['label'],source=r['source'],rep=r['rep'],
                       **round_progress(Path(r['log']).read_text())) for r in rows
                      if r['phase']=='timing' and r['binary']!='wasp_sssp']
    data['audit_script_sha256']=sha256(__file__)
    data['raw_directory']=str(out)
    training_sources=sorted({r['source'] for r in rows if r['phase']=='train'})
    labels=list(data['selection']['scores'] if queue else data['selection']['screen_scores'])
    data['training']={label:[dict(source=src,
        seconds=median(rows,label,src,phase='train'),
        scans=median(rows,label,src,'scan_factor','train'),
        rounds=median(rows,label,src,'rounds','train')) for src in training_sources] for label in labels}
    calculated={label:statistics.geometric_mean(v['seconds'] for v in vals)
                for label,vals in data['training'].items()}
    assert calculated==(data['selection']['scores'] if queue else data['selection']['screen_scores'])
    if not queue:
        for label,score in data['selection']['confirmation_scores'].items():
            assert score==statistics.geometric_mean(median(rows,label,src,phase='confirm') for src in training_sources)
    data['ranges']={}
    for label in data['sources'][0]['variants']:
        source_data=[x['variants'] for x in data['sources']]
        ratios=[v['base']['median']/v[label]['median'] for v in source_data]
        wasp=[v['wasp']['median']/v[label]['median'] for v in source_data]
        data['ranges'][label]=dict(seconds=[min(v[label]['median'] for v in source_data),max(v[label]['median'] for v in source_data)],
            speedup_over_base=[min(ratios),max(ratios)],geomean_speedup=statistics.geometric_mean(ratios),
            speedup_over_wasp=[min(wasp),max(wasp)])
    lines=[f'# Delta road {"queue" if queue else "layout"} study','',
           f"Job {data['manifest']['job']} on {data['manifest']['hosts']}: {len(rows)} full solves validated.",
           'All ACIC solves use +old-scheduler and have verified worker affinity. Medians of three timed launches per test source.','',
           'Training-only selection: '+json.dumps(data['selection']['winner']), '',
           '| Training configuration | Geomean seconds | Source scans | Source rounds |',
           '|---|---:|---|---|']
    for label in sorted(calculated,key=calculated.get):
        vals=data['training'][label]
        lines.append(f"| {label} | {calculated[label]:.6f} | {', '.join(format(v['scans'],'.3f') for v in vals)} | {', '.join(str(v['rounds']) for v in vals)} |")
    labels=list(data['sources'][0]['variants'])
    lines+=['','| Source | '+' | '.join(labels)+' |','|---:|'+'---:|'*len(labels)]
    for source in data['sources']:
        lines.append('| '+source['source']+' | '+' | '.join(f"{source['variants'][l]['median']:.6f}" for l in labels)+' |')
    lines+=['','Speedup is reference/ACIC; larger means faster.','',
            '| Arm | Speedup over original range | Geomean | Speedup over Wasp range |','|---|---:|---:|---:|']
    for label in labels:
        r=data['ranges'][label]
        lines.append(f"| {label} | {r['speedup_over_base'][0]:.3f}–{r['speedup_over_base'][1]:.3f} | {r['geomean_speedup']:.3f} | {r['speedup_over_wasp'][0]:.3f}–{r['speedup_over_wasp'][1]:.3f} |")
    lines+=['','Diagnostic work, separately instrumented:','',
            '| Arm/source | Scans | Idle share | Pop calls M | Updates/pop | Queue estimated share | Published items M | Peer items M | Partial chunks | Private items/sample | Bands/sample |',
            '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    for r in data['work']:
        c=r['counters']; comm=r['comm']; q=r['queue']
        n=c.get('chunk_private_samples',0)
        optional=lambda key,denom=1: f"{c[key]/denom:.3f}" if key in c and denom else 'unmeasured'
        lines.append(f"| {r['label']}/{r['source']} | {r['scan_factor']:.3f} | {comm['idle_share']:.3f} | {c['pop_calls']/1e6:.3f} | {c['queue_pops']/c['pop_calls']:.3f} | {sum(v['estimated_share'] for v in q.values()):.3f} | {optional('chunk_published_items',1e6)} | {optional('chunk_peer_taken_items',1e6)} | {optional('chunk_partial_publications')} | {optional('chunk_private_items',n)} | {optional('chunk_private_bands',n)} |")
    lines+=['','Private occupancy samples describe the owner at pop entry, not a time-weighted global occupancy. Failed-search counters count scans that find no published chunk; a taken chunk with no currently admitted item can also yield an empty pop. Counter/entry/idle shares overlap and are not additive wall-time components.','']
    (out/'audit.json').write_text(json.dumps(data,indent=2)+'\n')
    (out/'report.md').write_text('\n'.join(lines))
    print(out/'report.md')
    print(json.dumps(data['ranges'],indent=2))
    return data


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory',type=Path)
    ap.add_argument('--archive',type=Path)
    args=ap.parse_args()
    data=audit(args.directory)
    if args.archive: args.archive.write_text(json.dumps(data,indent=2)+'\n')
