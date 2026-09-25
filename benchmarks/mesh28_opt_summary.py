#!/usr/bin/env python3
"""Combine held-out times, Wasp, work ledgers, PC probes and optimized traces."""
import argparse,hashlib,json,statistics
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('campaign',type=Path);ap.add_argument('job');a=ap.parse_args();root=a.campaign;job=a.job
held=json.loads((root/'logs'/('AB-mesh28-z-1n-'+job)/'audit.json').read_text())
wasp=json.loads((root/'logs'/('wasp-fixed-'+job)/'runs.json').read_text())
work=json.loads((root/'logs'/('work-'+job)/'runs.json').read_text())
pc=json.loads((root/'profiles'/('pc-'+job)/'runs.json').read_text())
trace=root/'traces'/('optimized-'+job);truns=json.loads((trace/'runs.json').read_text());proj=json.loads((trace/'projections_report.json').read_text());backlog=json.loads((trace/'heap_backlog_report.json').read_text())
assert held['validated']==64 and len(wasp)==16 and len(work)==4 and len(pc)==12 and len(truns)==3
assert all(r['valid'] for rs in [wasp,work,pc,truns] for r in rs)
assert proj['pes']==112 and len(list(trace.glob('*.log.gz')))==112
summary=dict(job=job,hosts=held['manifest']['hosts'],validated_full_graph_solves=99,selected=json.loads((root/'configs/selected.json').read_text()),sources=[],work=work,pc_runs=pc,pc_overhead={},trace_runs=truns,projections=proj,heap_backlog={k:v for k,v in backlog.items() if k!='per_pe'},campaign=str(root))
summary['analysis_scripts'] = {
 name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
 for name in ['mesh28_opt_summary.py', 'papi_profile_report.py',
              'projections_report.py', 'heap_backlog_report.py',
              'mesh28_pc_probe.py', 'mesh28_opt_plot.py']}
summary['heldout_audit'] = held
summary['wasp_runs'] = wasp
summary['runtime'] = json.loads((root/'protocol/runtime-manifest.json').read_text())
summary['build_manifests'] = {
    name: (root/'bin'/(name+'.manifest')).read_text()
    for name in ['acic_base', 'acic_base_cost', 'acic_base_papi',
                 'acic_chunks_256', 'acic_chunks_256_cost',
                 'acic_chunks_256_papi', 'acic_chunks_256_prj']}
summary['graph'] = dict(name='mesh28-z', vertices=268435456,
    stored_directed_edges=1073676288, seed=1, ordering='Morton',
    sha256='31f961b22ad6944a7285225112802304f45ff8d235736cc830c474b2575686e2')
summary['methods'] = dict(
    timing='One warmup and three measured solves per source and arm; ACIC arm order randomized within repetitions; medians reported.',
    layout='One exclusive 128-core Delta cpu-interactive node. ACIC 16 processes x 7 workers, +old-scheduler; Wasp 128 threads, delta 4096.',
    pc='Two solve-window samples per variant, requested thread-CPU timer period 250 microseconds, bracketed by production and timer-off controls. No PMU hardware counters.',
    correctness='Every solve checked against independent GAPBS-reader serial Dijkstra digest, reachable count and distance sum. Separate work builds check queue/retirement conservation.',
    scope='One-node mesh result only; no multi-node, road, RMAT or cross-machine validation of the experimental queue.')
for index,values in held['summary'].items():
 source=next(r['source'] for r in held['runs'] if r['source_index']==int(index));w=[r['seconds'] for r in wasp if r['source']==source and r['rep']>=0];assert len(w)==3
 wm=statistics.median(w);best=values['chunks64']['median'];base=values['base']['median']
 summary['sources'].append(dict(source=source,variants=values,wasp_seconds=w,wasp_median=wm,speedup_over_original=base/best,time_reduction=1-best/base,wasp_speedup_over_acic=best/wm,acic_speedup_over_wasp=wm/best))
for v in ['base','chunks64']:
 rs=[r for r in pc if r['variant']==v];plain=statistics.mean(r['seconds'] for r in rs if r['kind']=='plain')
 summary['pc_overhead'][v]=dict(plain_mean=plain,sampled=[r['seconds']/plain for r in rs if r['kind']=='sampled'],profile_off=[r['seconds']/plain for r in rs if r['kind']=='off'],samples=[r['samples'] for r in rs if r['kind']=='sampled'])
summary['pc_findings'] = []
for row in pc:
 if row['kind'] != 'sampled': continue
 path = root/'profiles'/('pc-'+job)/(row['label']+'.json')
 d = json.loads(path.read_text()); inner=d['innermost']; total=d['samples']
 assert d['ambiguous_mapping_samples']==0 and d['unmapped_samples']==0 and d['dropped']==0
 groups = dict(
   mutex_futex=sum(n for k,n in inner.items() if 'mutex' in k or '__lll_lock' in k),
   tls=sum(n for k,n in inner.items() if '__tls_get_addr' in k),
   ownership_runtime=sum(n for k,n in inner.items() if k in ['CmiNodeOf','CmiMyNode','CmiNodeFirst','CmiGetState']),
   ownership_application=sum(n for k,n in inner.items() if k in ['SsspChares::process_partition','SsspChares::get_dest_proc_fast']),
   shared_update=inner.get('SsspChares::process_shared_update',0),
   edge_scan=inner.get('SsspChares::send_relaxations',0))
 summary['pc_findings'].append(dict(label=row['label'],report=str(path),samples=total,
   dropped=d['dropped'],groups_percent={k:100*n/total for k,n in groups.items()},
   top_innermost=sorted(inner.items(),key=lambda x:x[1],reverse=True)[:20]))
summary['pc_interpretation'] = (
 'Fractions are statistical sample attribution, not calibrated CPU nanoseconds. '
 'Requested 250us CPU timer delivery can coalesce. Shared-library names use nearest '
 'available symbols; stripped internal routines need broad interpretation. '
 'No sampled address had ambiguous or missing executable mappings in the four captures. '
 'These are leaf/inline samples, not call-stack profiles of runtime helpers.')
summary['trace_slowdown']=truns[1]['seconds']/statistics.mean(r['seconds'] for r in [truns[0],truns[2]])
(root/'logs'/('summary-'+job+'.json')).write_text(json.dumps(summary,indent=2)+'\n')
lines=['# Delta mesh28 optimization confirmation','',f"Job {job}, {summary['hosts']}; 99 full graph solves validated.",'','| Source | Original s | Chunks/slice8 s | Chunks/slice64 s | Original/new | Wasp s | New/Wasp time |','|---:|---:|---:|---:|---:|---:|---:|']
for r in summary['sources']:
 v=r['variants'];lines.append(f"| {r['source']} | {v['base']['median']:.3f} | {v['chunks8']['median']:.3f} | {v['chunks64']['median']:.3f} | {r['speedup_over_original']:.2f}x | {r['wasp_median']:.3f} | {r['wasp_speedup_over_acic']:.2f}x |")
lines+=['',f"Trace slowdown: {summary['trace_slowdown']:.3f}x. Trace root: {trace}/acic_chunks_256_prj.",'',f"PC overhead: {json.dumps(summary['pc_overhead'])}",'','Queue costs are inclusive sampled estimates, not additive to Projections entry times. Wasp is remeasured in the same allocation after the ACIC paired runs. Results establish one-node performance; multi-node performance remains unmeasured for the experimental queue.']
(root/'logs'/('summary-'+job+'.md')).write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
