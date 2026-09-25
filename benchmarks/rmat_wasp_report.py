#!/usr/bin/env python3
"""Reaudit the Delta RMAT run and report source-matched solve-time comparisons."""
import argparse
import json
from pathlib import Path
import statistics
from rmat_wasp import validate
from road_experiment import sha256


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory',type=Path)
    ap.add_argument('--archive',type=Path)
    a=ap.parse_args();out=a.directory.resolve();root=out.parents[1]
    data=json.loads((out/'summary.json').read_text())
    rows=[json.loads(x) for x in (out/'runs.jsonl').read_text().splitlines()]
    cfg=data['manifest']['protocol'];selection=data['selection']
    grid=[dict(threads=t,delta=d) for t in cfg['wasp_threads'] for d in cfg['wasp_deltas']]
    expected=len(cfg['variants'])+1+len(grid)*2+4+(len(cfg['variants'])+1)*16
    assert len(rows)==expected==cfg['planned_solves']==data['validated_solves']
    reference=root/'graphs'/(cfg['graph']+'.reference.txt')
    assert sha256(reference)==data['manifest']['graph_hashes']['.reference.txt']
    seen=set()
    for row in rows:
        key=(row['label'],row['source'],row['phase'],row['rep'],json.dumps(row['setting'],sort_keys=True))
        assert key not in seen;seen.add(key)
        assert row['valid'] and row['returncode']==0
        for k,v in validate(reference,row).items():assert row[k]==v
    train=[r[0] for r in data['manifest']['references'] if r[1]=='tune'][:2]
    def score(setting,phases):
        return statistics.geometric_mean(statistics.median(r['seconds'] for r in rows
            if r['source']==source and r['phase'] in phases and r['setting']==setting) for source in train)
    finalists=sorted(grid,key=lambda setting:score(setting,['tune']))[:2]
    assert selection['winner']==min(finalists,key=lambda setting:score(setting,['tune','confirm']))
    for setting in selection['scores']:
        assert setting['geomean_seconds']==score({k:setting[k] for k in ['threads','delta']},['tune','confirm'])
    labels=['base','current','control','wasp']
    ranges={label:[] for label in labels}
    current_vs_base=[];controls=[]
    lines=['# Delta RMAT25 / Wasp comparison','',
           f"Job {data['manifest']['job']} on {data['manifest']['hosts']}: {len(rows)} validated full solves.",
           'ACIC:16 processes ×7 workers, +old-scheduler. Sharing/chunks off; lazy relaxation and degree256 hub hints on.',
           f"Training-selected Wasp: {selection['winner']['threads']} threads, delta {selection['winner']['delta']}.",
           'Four test sources; one warmup and three measured launches per arm/source. Solve-only time.',
           'The current binary has private chunks compiled in, but RMAT does not activate them. This is a regression/comparison result, not a chunk-optimization claim.','',
           '| Source | Original ACIC s | Current ACIC s | Original control s | Wasp s | Current speedup over Wasp |',
           '|---:|---:|---:|---:|---:|---:|']
    for source in data['sources']:
        v=source['variants']
        for label in labels:
            raw=[r['seconds'] for r in rows if r['source']==source['source'] and r['label']==label and r['phase']=='timing']
            assert len(raw)==3 and raw==v[label]['seconds']
            assert statistics.median(raw)==v[label]['median']
            ranges[label].append(v[label]['median'])
        source['current_speedup_over_wasp']=v['wasp']['median']/v['current']['median']
        source['current_speedup_over_original']=v['base']['median']/v['current']['median']
        current_vs_base.append(source['current_speedup_over_original'])
        controls.append(v['base']['median']/v['control']['median'])
        lines.append('| '+source['source']+' | '+' | '.join(f"{v[label]['median']:.6f}" for label in labels)+f" | {source['current_speedup_over_wasp']:.3f}× |")
    speedups=[s['current_speedup_over_wasp'] for s in data['sources']]
    data['ranges']={label:[min(values),max(values)] for label,values in ranges.items()}
    data['current_speedup_over_wasp']=[min(speedups),max(speedups)]
    data['current_geomean_speedup_over_wasp']=statistics.geometric_mean(speedups)
    data['current_speedup_over_original']=[min(current_vs_base),max(current_vs_base)]
    data['original_control_speedup']=[min(controls),max(controls)]
    data['audit_code']={name:sha256(Path(__file__).with_name(name)) for name in
                        ['rmat_wasp_report.py','rmat_wasp.py','road_experiment.py','road_layout_launch.py','check_onenode_digest.py']}
    data['raw_directory']=str(out)
    lines+=['',f"Current/original speedup: {min(current_vs_base):.3f}–{max(current_vs_base):.3f}×; duplicate-original control ratios: {min(controls):.3f}–{max(controls):.3f}×.",
            f"Current speedup over Wasp: {min(speedups):.3f}–{max(speedups):.3f}×; geometric mean {statistics.geometric_mean(speedups):.3f}×.",
            'Speedup is reference time / ACIC time; above1 means ACIC is faster.',
            f"Wasp bounded-grid boundary flags: {selection['boundary']}. The search does not establish a global optimum.",
            'Lazy tokens and hints are recorded in raw logs. No non-lazy ledger estimate is used for edge work.','']
    (out/'audit.json').write_text(json.dumps(data,indent=2)+'\n')
    (out/'report.md').write_text('\n'.join(lines))
    if a.archive:a.archive.write_text(json.dumps(data,indent=2)+'\n')
    print('\n'.join(lines))


if __name__=='__main__':main()
