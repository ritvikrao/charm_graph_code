#!/usr/bin/env python3
"""Audit complete R1 job matrices, raw solves, launch settings and work counts."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re

import machine

from check_onenode_digest import check
from onenode_report import fields
from summarize_r0 import cells, ratio, read_rows, summarize
from work_cost_report import check_work_accounting, production_attempts


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign', type=Path)
    ap.add_argument('jobs', nargs='+', type=int)
    ap.add_argument('--config', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--graphs', default='mesh26-z,road-usa-z')
    ap.add_argument('--sources', type=int, default=2)
    ap.add_argument('--source-role', default='tune')
    args = ap.parse_args()
    root = args.campaign
    config = json.loads(args.config.read_text())
    hashed = [{**v, 'sha256': hashlib.sha256((root/'bin'/v['binary']).read_bytes()).hexdigest()}
              for v in config]
    result = dict(allocations=[], checked_solves=0, diagnostic_solves=0,
                  checked_launches=0, raw_sha256={})
    for job in args.jobs:
        for graph in args.graphs.split(','):
            directories = list((root/'logs').glob(f'AB-{graph}-*n-{job}'))
            if len(directories) != 1:
                raise ValueError(f'{job}/{graph}: expected one result directory')
            directory = directories[0]
            manifest = json.loads((directory/'manifest.json').read_text())
            # Variants without an explicit layout use the whole allocation:
            # 8 x 15 on Delta and Anvil, 8 x 7 on Frontier (machine.py).
            default_workers = machine.acic_workers(8)
            expected_variants = [{**v, 'nodes': v.get('nodes', manifest['nodes']),
                                  'rpn': v.get('rpn', 8), 'workers': v.get('workers', default_workers)} for v in hashed]
            if (manifest['variants'] != expected_variants or manifest['workers'] != default_workers
                    or manifest['rpn'] != 8 or manifest['sources'] != args.sources or manifest['reps'] != 3
                    or manifest['source_role'] != args.source_role or not manifest['batch']):
                raise ValueError(f'{directory}: unexpected experiment settings')
            refpath = root/'graphs'/f'{graph}.reference.txt'
            references = [l.split() for l in refpath.read_text().splitlines() if l[:1].isdigit()]
            references = [r for r in references if r[1] == args.source_role][:args.sources]
            sources = [r[0] for r in references]
            meta = (root/'graphs'/f'{graph}.meta').read_text()
            vertices = int(re.search(r'vertices=(\d+)',meta)[1])
            arcs = int(re.search(r'arcs=(\d+)',meta)[1])
            rows = read_rows(directory/'runs.jsonl')
            labels = [v['label'] for v in expected_variants]
            cells(rows, {(v, s, rep) for v in labels for s in sources for rep in range(-1,3)})
            warnings = Counter()
            for variant in expected_variants:
                for rep in range(-1,3):
                    launch = directory/f"{variant['label']}-g0-r{rep}.launch.out"
                    text = launch.read_text()
                    n, rpn, workers = variant['nodes'], variant['rpn'], variant['workers']
                    # Reconverse writes "1 process" for a single-process layout.
                    plural = 'process' if rpn*n == 1 else 'processes'
                    if (f'Starting Reconverse with {rpn*n} {plural}, {workers*n} PEs (1 PE = 1 thread), '
                            f'and {workers//rpn} PEs per process') not in text:
                        raise ValueError(f'{launch}: wrong worker layout')
                    # The last --slack-control value on the command line wins.
                    slack = [variant['flags'][i+1] for i,f in enumerate(variant['flags'])
                             if f == '--slack-control'][-1:] or ['off']
                    # Same rule as process_share_active() in sssp_smp.cpp: auto
                    # shares only below 8 arcs per vertex (mesh and road, not
                    # the RMAT regression graphs); the default is off.
                    share = [variant['flags'][i+1] for i,f in enumerate(variant['flags'])
                             if f == '--process-share'][-1:] or ['off']
                    sharing = 'on' if share[0] == 'on' or (share[0] == 'auto' and arcs < 8*vertices) else 'off'
                    if re.findall(r'Process sharing: ([^\r\n]*)',text) != [sharing]:
                        raise ValueError(f'{launch}: expected Process sharing: {sharing}')
                    if f'Live slack: {slack[0]}' not in text:
                        raise ValueError(f'{launch}: missing effective setting Live slack: {slack[0]}')
                    # Reader tiling prints its line only when active; auto tiles
                    # under the same 8-arcs-per-vertex rule, and the default is off.
                    tile = [variant['flags'][i+1] for i,f in enumerate(variant['flags'])
                            if f == '--reader-tile'][-1:] or ['off']
                    tiled = arcs < 8*vertices if tile[0] == 'auto' else tile[0] not in ('off','0')
                    if len(re.findall(r'^Reader tiles: ',text,flags=re.M)) != tiled:
                        raise ValueError(f'{launch}: expected reader tiles {"on" if tiled else "off"}')
                    if re.findall(r'Live slack: ([^\r\n]*)',text) != slack:
                        raise ValueError(f'{launch}: wrong live slack setting')
                    # The binary marks queue settings "(inactive)" when not sharing.
                    inactive = '' if sharing == 'on' else ' (inactive)'
                    if '--process-queue' in variant['flags']:
                        policy = variant['flags'][variant['flags'].index('--process-queue')+1]
                        if re.findall(r'Process queue: ([^\r\n]*)',text) != [policy + inactive]:
                            raise ValueError(f'{launch}: wrong queue policy')
                    if '--process-queue-batch' in variant['flags']:
                        batch = variant['flags'][variant['flags'].index('--process-queue-batch')+1]
                        if re.findall(r'Process queue batch: ([^\r\n]*)',text) != [batch + inactive]:
                            raise ValueError(f'{launch}: wrong queue batch size')
                    chunks = re.split(r'^SOURCE_RUN index=\d+ source=(\d+)\n',text,flags=re.M)
                    if chunks[1::2] != sources:
                        raise ValueError(f'{launch}: missing or reordered sources')
                    for index, chunk in enumerate(chunks[2::2]):
                        if chunk != (directory/f"{variant['label']}-s{index}-r{rep}.out").read_text():
                            raise ValueError(f'{launch}: extracted source log changed')
                    warnings.update(':'.join(x) for x in re.findall(r'<lci:(warn|error):([^>]+)>',text))
                    cleaned = re.sub(r'UCX registration cache is not available.*?enable UCX memory hooks\.', '', text)
                    cleaned = cleaned.replace('<lci:warn:reg_cache>', '')
                    if re.search(r'(?i)\b(error|warning|fatal|abort|failed|timeout|killed|segmentation|rescue|conservation violated|progress_stall)\b',cleaned):
                        raise ValueError(f'{launch}: unexpected runtime diagnostic; inspect raw log')
                    result['raw_sha256'][str(launch.relative_to(root))] = hashlib.sha256(launch.read_bytes()).hexdigest()
                    result['checked_launches'] += 1
            has_diag = any(v.get('work_cost') for v in expected_variants)
            diagnostic = json.loads((directory/'work-cost.json').read_text()) if has_diag else []
            cells(diagnostic, {(v['label'], s, rep) for v in expected_variants if v.get('work_cost')
                               for s in sources for rep in range(-1,3)})
            diag_by_cell = {(r['variant'],str(r['source']),r['rep']): r for r in diagnostic}
            output = []
            for row in rows:
                source = str(row['source'])
                if row['graph'] != graph or sources[row['source_index']] != source:
                    raise ValueError('source/graph mismatch')
                path = directory/f"{row['variant']}-s{row['source_index']}-r{row['rep']}.out"
                text = path.read_text()
                check(refpath, source, path)
                times = re.findall(r'Compute time: ([\d.eE+-]+)',text)
                if (not row['valid'] or len(times) != 1 or float(times[0]) != row['seconds']
                        or not math.isfinite(row['seconds']) or row['seconds'] <= 0):
                    raise ValueError(f'{path}: inconsistent solve time')
                metrics = dict(seconds=row['seconds'])
                # The ledger formula holds only on the non-lazy path; auto
                # lazy-heavy is on for the dense RMAT graphs, so those rows are
                # timed and digest-checked but carry no work count.
                if 'Lazy heavy:' not in text:
                    attempts = production_attempts(text,vertices)
                    metrics.update(edge_attempts=attempts,
                        attempts_per_edge=attempts/int(references[row['source_index']][7]))
                key = (row['variant'],source,row['rep'])
                if key in diag_by_cell:
                    c = check_work_accounting(text,vertices)
                    d = diag_by_cell[key]
                    if c != d['counters'] or d['seconds'] != row['seconds'] or fields(text,'COMM_SHARE') != d['comm']:
                        raise ValueError(f'{path}: diagnostic summary differs from raw log')
                    work = d['comm']['work_seconds']-d['comm']['work_send_seconds']
                    metrics.update(expansions_per_vertex=d['expansions_per_vertex'],
                        stale_pop_fraction=c['stale_pops']/c['queue_pops'],
                        cpu_ns_per_attempt=d['cpu_ns_per_attempt'],work_ns_per_attempt=d['work_ns_per_attempt'],
                        queue_ns_per_attempt=1e9*d['queue_estimated_seconds']/c['edge_attempts'],
                        queue_work_fraction=d['queue_estimated_seconds']/work,
                        lock_miss_fraction=c['lock_misses']/c['queue_probes'],
                        work_share=d['comm']['compute_share'],idle_share=d['comm']['idle_share'],
                        send_share=d['comm']['send_share'],
                        inter_node_fraction=c['inter_node']/c['edge_attempts'],
                        pops_per_removal_call=c['queue_pops']/c['pop_calls'],
                        pop_queue_time_fraction=(c['pop_ticks']*c['pop_calls']/max(1,c['pop_samples'])) /
                            max(1, c['pop_ticks']*c['pop_calls']/max(1,c['pop_samples']) +
                                   c['push_ticks']*c['push_calls']/max(1,c['push_samples'])))
                    result['diagnostic_solves'] += 1
                elif 'WORK_COST ' in text:
                    raise ValueError(f'{path}: unexpected diagnostic build')
                output.append(dict(variant=row['variant'],source=source,rep=row['rep'],metrics=metrics))
                result['raw_sha256'][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
                result['checked_solves'] += 1
            summary = summarize(output)
            comparisons = {f'{a}/{b}:{metric}': ratio(summary,a,b,metric)
                for a,b in [('nearest','local'),('nearest','control'),('nearest','frozen_r0'),
                            ('control','local'),('control','nearest'),('local','frozen_r0'),
                            ('nearest_diag','nearest'),('nearest','frozen_nearest'),
                            ('batch8','nearest'),('batch32','nearest'),
                            ('batch8','control'),('batch32','control'),
                            ('batch8','frozen_r0'),('batch32','frozen_r0'),
                            ('batch8_diag','batch8'),('batch32_diag','batch32'),
                            ('batch8_control','batch8'),('batch8','batch8_control'),
                            ('batch32','batch8'),('batch32','batch8_control'),
                            # Layout attribution chain, see ipdps27-r1-attribution.md.
                            ('n1_16x7','n1_8x15'),('n2_8x7','n1_16x7'),('n2_8x15','n2_8x7'),
                            ('n2_8x15','n1_8x15'),('n1_8x15_control','n1_8x15'),
                            ('n1_16x7_diag','n1_8x15_diag'),('n2_8x7_diag','n1_16x7_diag'),
                            ('n2_8x15_diag','n2_8x7_diag'),('n2_8x15_diag','n1_8x15_diag')]
                if a in labels and b in labels
                for metric in ('seconds','attempts_per_edge') if metric in summary[a]['medians']}
            result['allocations'].append(dict(job=job,graph=graph,manifest=manifest,
                runtime_warnings=dict(warnings),summary=summary,comparisons=comparisons,rows=output))
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print({k:result[k] for k in ('checked_solves','diagnostic_solves','checked_launches')})
    for a in result['allocations']:
        print(a['job'],a['graph'])
        print('medians', {k:{m:round(v,4) for m,v in s['medians'].items()} for k,s in a['summary'].items()})
        print('paired ratios', {k:round(v['median'],4) for k,v in a['comparisons'].items()})


if __name__ == '__main__':
    main()
