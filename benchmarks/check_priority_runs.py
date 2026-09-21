#!/usr/bin/env python3
"""Audit complete R1 job matrices, raw solves, launch settings and work counts."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re

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
    args = ap.parse_args()
    root = args.campaign
    config = json.loads(args.config.read_text())
    expected_variants = [{**v, 'sha256': hashlib.sha256((root/'bin'/v['binary']).read_bytes()).hexdigest()}
                         for v in config]
    result = dict(allocations=[], checked_solves=0, diagnostic_solves=0,
                  checked_launches=0, raw_sha256={})
    for job in args.jobs:
        for graph in ('mesh26-z', 'road-usa-z'):
            directories = list((root/'logs').glob(f'AB-{graph}-*n-{job}'))
            if len(directories) != 1:
                raise ValueError(f'{job}/{graph}: expected one result directory')
            directory = directories[0]
            manifest = json.loads((directory/'manifest.json').read_text())
            if (manifest['variants'] != expected_variants or manifest['workers'] != 120
                    or manifest['rpn'] != 8 or manifest['sources'] != 2 or manifest['reps'] != 3
                    or manifest['source_role'] != 'tune' or not manifest['batch']):
                raise ValueError(f'{directory}: unexpected experiment settings')
            refpath = root/'graphs'/f'{graph}.reference.txt'
            references = [l.split() for l in refpath.read_text().splitlines() if l[:1].isdigit()]
            references = [r for r in references if r[1] == 'tune'][:2]
            sources = [r[0] for r in references]
            vertices = int(re.search(r'vertices=(\d+)',(root/'graphs'/f'{graph}.meta').read_text())[1])
            rows = read_rows(directory/'runs.jsonl')
            labels = [v['label'] for v in expected_variants]
            cells(rows, {(v, s, rep) for v in labels for s in sources for rep in range(-1,3)})
            warnings = Counter()
            for variant in expected_variants:
                for rep in range(-1,3):
                    launch = directory/f"{variant['label']}-g0-r{rep}.launch.out"
                    text = launch.read_text()
                    nodes = manifest['nodes']
                    if f'Starting Reconverse with {8*nodes} processes, {120*nodes} PEs (1 PE = 1 thread), and 15 PEs per process' not in text:
                        raise ValueError(f'{launch}: wrong worker layout')
                    for marker in ['Process sharing: on','Live slack: off','Reader tiles:']:
                        if marker not in text:
                            raise ValueError(f'{launch}: missing effective setting {marker}')
                    if '--process-queue' in variant['flags']:
                        policy = variant['flags'][variant['flags'].index('--process-queue')+1]
                        if re.findall(r'Process queue: ([^\r\n]*)',text) != [policy]:
                            raise ValueError(f'{launch}: wrong queue policy')
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
            diagnostic = json.loads((directory/'work-cost.json').read_text())
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
                attempts = production_attempts(text,vertices)
                metrics = dict(seconds=row['seconds'],edge_attempts=attempts,
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
                        queue_work_fraction=d['queue_estimated_seconds']/work,
                        lock_miss_fraction=c['lock_misses']/c['queue_probes'])
                    result['diagnostic_solves'] += 1
                elif 'WORK_COST ' in text:
                    raise ValueError(f'{path}: unexpected diagnostic build')
                output.append(dict(variant=row['variant'],source=source,rep=row['rep'],metrics=metrics))
                result['raw_sha256'][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
                result['checked_solves'] += 1
            summary = summarize(output)
            comparisons = {f'{a}/{b}:{metric}': ratio(summary,a,b,metric)
                for a,b in [('nearest','local'),('nearest','control'),('nearest','frozen_r0'),
                            ('control','local'),('local','frozen_r0'),('nearest_diag','nearest')]
                for metric in ('seconds','attempts_per_edge')}
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
