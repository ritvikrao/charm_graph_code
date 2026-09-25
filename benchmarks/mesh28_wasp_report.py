#!/usr/bin/env python3
"""Audit and summarize the frozen Delta mesh28 Wasp/ACIC experiment."""
import argparse
import json
from pathlib import Path
import statistics
import re
from check_onenode_digest import check
from work_cost_report import check_work_accounting


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory',type=Path)
    args=ap.parse_args()
    out=args.directory.resolve();root=out.parents[1]
    rows=[json.loads(l) for l in (out/'runs.jsonl').read_text().splitlines()]
    manifest=json.loads((out/'manifest.json').read_text())
    assert len(rows)==62 and all(r['valid'] and r['returncode']==0 for r in rows)
    for r in rows:
        check(root/'graphs/mesh28-z.reference.txt',r['source'],r['log'])
        if r['engine']=='acic_cost': check_work_accounting(Path(r['log']).read_text(),268435456)
    selected=json.loads((out/'wasp-selected.json').read_text())
    result=dict(job=manifest['job'],hosts=manifest['hosts'],validated_solves=len(rows),wasp_selection=selected,manifest=str(out/'manifest.json'),sources=[])
    lines=['# Delta mesh28-z: ACIC versus Wasp','',f"Job {manifest['job']}, {manifest['hosts']}; {len(rows)} solves pass the independent full digest.",f"Wasp setting: {selected['winner']}; ACIC: 16 × 7, nearest, batch 8, slice 8, +old-scheduler.",'','| Source | ACIC median s (range) | Wasp median s (range) | Wasp / ACIC | ACIC scans / stored edge | ACIC rounds |','|---:|---:|---:|---:|---:|---:|']
    for source in sorted(set(r['source'] for r in rows if r['phase']=='timing')):
        a=[r for r in rows if r['source']==source and r['phase']=='timing' and r['engine']=='acic']
        w=[r for r in rows if r['source']==source and r['phase']=='timing' and r['engine']=='wasp_sssp']
        assert len(a)==len(w)==3
        at=[r['seconds'] for r in a]; wt=[r['seconds'] for r in w]
        item=dict(source=source,acic_seconds=at,wasp_seconds=wt,acic_median=statistics.median(at),wasp_median=statistics.median(wt),acic_scan_factors=[r['edge_scan_factor'] for r in a],acic_rounds=[r['rounds'] for r in a])
        item['acic_speedup_over_wasp']=item['wasp_median']/item['acic_median']
        result['sources'].append(item)
        lines.append(f"| {source} | {item['acic_median']:.3f} ({min(at):.3f}–{max(at):.3f}) | {item['wasp_median']:.3f} ({min(wt):.3f}–{max(wt):.3f}) | {item['acic_speedup_over_wasp']:.3f}× | {statistics.median(item['acic_scan_factors']):.3f} | {statistics.median(item['acic_rounds']):.0f} |")
    result['work']=[r for r in rows if r['phase']=='work']
    lines+=['','## Separate diagnostic runs','']
    for a in result['work']:
        if a['engine']!='acic_cost': continue
        w=next(r for r in result['work'] if r['engine']=='wasp_cost' and r['source']==a['source'])
        c=a['counters']
        clock=re.search(r'WORK_CLOCK window_ticks=(\d+) window_us=(\d+)',Path(a['log']).read_text())
        ticks,us=map(int,clock.groups())
        a['sampled_queue_cost']={}
        for kind in ['push','pop']:
            average=c[kind+'_ticks']/c[kind+'_samples']
            a['sampled_queue_cost'][kind]=dict(cycles_per_call=average,ns_per_call=average*us*1000/ticks,estimated_pe_seconds=average*c[kind+'_calls']*us/ticks/1e6,estimated_window_share=average*c[kind+'_calls']/ticks)
        a['sampled_queue_cost']['combined_estimated_window_share']=sum(a['sampled_queue_cost'][k]['estimated_window_share'] for k in ['push','pop'])
        lines.append(f"Inclusive sampled queue time is about {100*a['sampled_queue_cost']['combined_estimated_window_share']:.1f}% of the PE window (calibrated with this run's WORK_CLOCK). This estimate overlaps process_heap time; it includes locking, ordered containers, hint scans, and queue bookkeeping, and is not solely lock wait.")
        lines.append(f"Source {a['source']}: ACIC {c['edge_attempts']:,} edge attempts, {c['expansions']:,} expansions, {c['queue_pops']:,} pops ({100*c['stale_pops']/c['queue_pops']:.1f}% stale). Pop lock misses {c['lock_misses']:,}/{c['queue_probes']:,} probes ({100*c['lock_misses']/c['queue_probes']:.1f}%). CAS failures {100*c['cas_failures']/max(1,c['cas_attempts']):.4f}%. Intra-process attempts {100*c['intra_process']/c['edge_attempts']:.2f}%.")
        lines.append(f"Wasp counted {w['edge_inspections']:,} pull+push edge inspections. On this degree-2/3/4 mesh every expansion inspects each neighbor once by pull and once by push, so outgoing push inspections are half that count ({w['edge_inspections']//2:,}).")
        lines.append(f"Queue push sampled cycles/call: {c['push_ticks']/c['push_samples']:.1f}; pop: {c['pop_ticks']/c['pop_samples']:.1f}. Inclusive sampling; no inference that lock waiting accounts for all these cycles.\n")
    trace=next(r for r in rows if r['phase']=='trace')
    controls=[r for r in rows if r['phase'].startswith('trace-control')]
    result['trace']=dict(seconds=trace['seconds'],edge_attempts=trace['edge_attempts'],rounds=trace['rounds'],controls=controls,slowdown=trace['seconds']/statistics.mean(r['seconds'] for r in controls),projections_root=str(root/'traces'/('mesh28-z-'+manifest['job'])/'acic_prj'))
    result['projections']=json.loads((out/'projections_corrected.json').read_text())
    result['heap_backlog']=json.loads((out/'heap_backlog_report.json').read_text())
    lines+=['','## Trace validity',f"Traced solve {trace['seconds']:.3f}s; plain controls {[r['seconds'] for r in controls]}; tracing slowdown {result['trace']['slowdown']:.3f}×. Attempts: traced {trace['edge_attempts']:,}; controls {[r['edge_attempts'] for r in controls]}. Rounds: traced {trace['rounds']}; controls {[r['rounds'] for r in controls]}.",f"Projections root: `{result['trace']['projections_root']}`.",'','PE-time shares are not critical-path fractions. Untraced/idle regions can include ACIC idle-callback work; the separate COMM_SHARE timers cover it. Cross-process send-to-execute latencies use estimated clock offsets. One allocation does not measure between-allocation variation.']
    (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    (out/'summary.md').write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines))


if __name__=='__main__': main()
