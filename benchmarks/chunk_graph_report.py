#!/usr/bin/env python3
"""Reaudit every raw solve and summarize the road/uniform transfer check."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

from check_onenode_digest import check
from work_cost_report import check_work_accounting, production_attempts



def round_progress(text):
    pattern = (r'Updates: created: (\d+), noted: (\d+), processed: (\d+),.*?'
               r'Heap threshold: (-?\d+), Tram: (-?\d+),.*?t= ([0-9.eE+-]+)')
    rows = [(int(c),int(n),int(p),int(h),int(t),float(time))
            for c,n,p,h,t,time in re.findall(pattern, text)]
    observations = text.count('Updates: created:')
    # The terminal accounting record omits threshold and timestamp fields.
    assert len(rows)==observations-1
    # Exclude the initial observation and final termination observation.
    out = dict(observations=observations, comparisons=max(0,len(rows)-1),
               unchanged_threshold=0, unchanged_threshold_with_update_activity=0,
               no_update_activity=0, changed_threshold=0)
    for previous, current in zip(rows, rows[1:]):
        active = current[0]!=previous[0] or current[2]!=previous[2]
        unchanged = current[3:5]==previous[3:5]
        out['unchanged_threshold'] += unchanged
        out['unchanged_threshold_with_update_activity'] += unchanged and active
        out['no_update_activity'] += not active
        out['changed_threshold'] += not unchanged
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    ap.add_argument('--boundary', type=Path, help='completed uniform Wasp boundary check')
    args = ap.parse_args()
    out = args.directory.resolve()
    root = out.parents[1]
    data = json.loads((out/'summary.json').read_text())
    rows = [json.loads(s) for s in (out/'runs.jsonl').read_text().splitlines()]
    specs = {s['name']:s for s in data['manifest']['protocol']['graphs']}
    expected = sum(4+1+len(data['manifest']['protocol']['wasp_threads'])*len(s['wasp_deltas'])*2+4+80+len(s['work_variants'])*2
                   for s in specs.values())
    assert len(rows)==expected, (len(rows), expected)
    seen = set()
    progress = []
    for row in rows:
        spec = specs[row['graph']]
        log = Path(row['log'])
        check(root/'graphs'/(row['graph']+'.reference.txt'), row['source'], log)
        assert row['valid'] and math.isfinite(row['seconds']) and row['seconds']>0
        key = (row['graph'], row['label'], row['phase'], row['rep'], row['source'],
               json.dumps(row['setting'], sort_keys=True))
        assert key not in seen
        seen.add(key)
        text = log.read_text()
        pattern = (r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)' if row['label']=='wasp'
                   else r'Compute time: ([0-9.eE+-]+)')
        times = re.findall(pattern, text)
        assert len(times)==1 and float(times[0])==row['seconds']
        if row['label']!='wasp':
            assert 'Using the original scheduler (+old-scheduler)' in text
            assert production_attempts(text, spec['vertices'])==row['edge_attempts']
            assert row['process_sharing']==spec['expected_process_sharing']
            if row['phase']=='timing':
                progress.append(dict(graph=row['graph'],label=row['label'],source=row['source'],rep=row['rep'],seconds=row['seconds'],**round_progress(text)))
            if row['phase']=='work':
                assert check_work_accounting(text, spec['vertices'])==row['counters']
    lines = ['# Delta road/uniform transfer check', '',
             f"Job {data['manifest']['job']}, {data['manifest']['hosts']}; {len(rows)} validated solves.",
             'ACIC: 16 processes × 7 workers, +old-scheduler; one exclusive cpu-interactive node.',
             'Source medians of three measured launches; one warmup per arm/source.', '']
    for graph, result in data['graphs'].items():
        assert result['validated_solves']==sum(r['graph']==graph for r in rows)
        winner = result['wasp_selection']['winner']
        lines += [f'## {graph}', '',
                  f"Process sharing {result['expected_process_sharing']}; Wasp {winner['threads']} threads, delta {winner['delta']}.", '',
                  '| Source | Original s | Control s | Chunks/s8 s | Chunks/s64 s | Original/new | Wasp s | Wasp/new |',
                  '|---:|---:|---:|---:|---:|---:|---:|---:|']
        metrics = {k:[] for k in ['original_seconds','selected_seconds','wasp_seconds',
                   'selected_speedup','chunk_only_speedup','incremental_slice_speedup',
                   'control_speedup','wasp_speedup','original_scans','selected_scans',
                   'original_rounds','selected_rounds']}
        for source in result['sources']:
            v = source['variants']
            for label, arm in v.items():
                raw = [r for r in rows if r['graph']==graph and r['source']==source['source'] and
                       r['label']==label and r['phase']=='timing']
                assert len(raw)==3 and [r['seconds'] for r in raw]==arm['seconds']
                assert statistics.median(arm['seconds'])==arm['median']
            base, new, wasp = (v[k]['median'] for k in ['base','chunks64','wasp'])
            values = dict(original_seconds=base,selected_seconds=new,wasp_seconds=wasp,
                          selected_speedup=base/new,chunk_only_speedup=base/v['chunks8']['median'],
                          incremental_slice_speedup=v['chunks8']['median']/new,
                          control_speedup=base/v['control']['median'],wasp_speedup=wasp/new,
                          original_scans=statistics.median(v['base']['scans']),
                          selected_scans=statistics.median(v['chunks64']['scans']),
                          original_rounds=statistics.median(v['base']['rounds']),
                          selected_rounds=statistics.median(v['chunks64']['rounds']))
            for key, value in values.items(): metrics[key].append(value)
            lines.append(f"| {source['source']} | {base:.6f} | {v['control']['median']:.6f} | "
                         f"{v['chunks8']['median']:.6f} | {new:.6f} | {base/new:.3f}× | {wasp:.6f} | {wasp/new:.3f}× |")
        result['ranges'] = {key:[min(v),max(v)] for key,v in metrics.items()}
        result['geomean_speedup'] = statistics.geometric_mean(metrics['selected_speedup'])
        lines += ['', 'Ranges across source medians: '+json.dumps(result['ranges']), '']
    data['round_progress'] = progress
    data['round_progress_note'] = 'Initial observation and final termination observation excluded. Unchanged threshold does not imply an empty round; update activity is created or processed count change.'
    data['analysis_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    data['validated_solves'] = len(rows)
    if args.boundary:
        bdir = args.boundary.resolve()
        boundary = json.loads((bdir/'summary.json').read_text())
        brows = [json.loads(s) for s in (bdir/'runs.jsonl').read_text().splitlines()]
        assert boundary['validated_solves']==len(brows)
        expected_boundary = (17 if len(boundary['training_scores'])==3 else 9) + (48 if boundary['sources'] else 0)
        assert len(brows)==expected_boundary
        assert len({(r['label'],r['source'],r['phase'],r['rep'],r['delta']) for r in brows})==len(brows)
        for row in brows:
            text = Path(row['log']).read_text()
            check(root/'graphs/uniform25.reference.txt', row['source'], row['log'])
            assert row['valid'] and math.isfinite(row['seconds']) and row['seconds']>0
            pattern = r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)' if row['label']=='wasp' else r'Compute time: ([0-9.eE+-]+)'
            times = re.findall(pattern,text)
            assert len(times)==1 and float(times[0])==row['seconds']
            if row['label']!='wasp':
                assert 'Process sharing: off' in text
                assert 'Using the original scheduler (+old-scheduler)' in text
                assert production_attempts(text, specs['uniform25']['vertices'])==row['edge_attempts']
        winner = boundary['winner']
        lines += ['## Uniform Wasp boundary verification', '',
                  f"Job {boundary['manifest']['job']}, {boundary['manifest']['hosts']}; "
                  f"{len(brows)} valid solves; selected {winner['threads']} threads / delta {winner['delta']}.", '',
                  '| Source | Original s | Chunks/s64 s | Wasp s | Original/new | Wasp/new |',
                  '|---:|---:|---:|---:|---:|---:|']
        for source in boundary['sources']:
            m = source['medians']
            for label in ['base','chunks64','wasp']:
                raw = [r['seconds'] for r in brows if r['phase']=='timing' and
                       r['source']==source['source'] and r['label']==label]
                assert len(raw)==3 and raw==source['seconds'][label]
                assert statistics.median(raw)==m[label]
            source['selected_speedup'] = m['base']/m['chunks64']
            source['acic_speedup_over_wasp'] = m['wasp']/m['chunks64']
            source['original_speedup_over_wasp'] = m['wasp']/m['base']
            lines.append(f"| {source['source']} | {m['base']:.6f} | {m['chunks64']:.6f} | "
                         f"{m['wasp']:.6f} | {source['selected_speedup']:.3f}× | "
                         f"{source['acic_speedup_over_wasp']:.3f}× |")
        boundary['raw_directory'] = str(bdir)
        data['uniform_wasp_boundary'] = boundary
        data['total_validated_solves'] = len(rows)+len(brows)
        lines += ['', f"Total across both jobs: {data['total_validated_solves']} valid solves.",
                  'The uniform baseline selection changed using training sources only; compare times within each allocation.']

    data['build_manifests'] = {p.name:p.read_text() for p in (root/'bin').glob('*.manifest')}
    data['raw_runs'] = str(out/'runs.jsonl')
    data['raw_directory'] = str(out)
    (out/'audit.json').write_text(json.dumps(data, indent=2)+'\n')
    (out/'report.md').write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines))


if __name__=='__main__':
    main()
