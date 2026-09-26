#!/usr/bin/env python3
"""Audit and combine the bounded Delta road-size/weight/feature/trace probes."""
import argparse
import json
import re
import statistics
import struct
from pathlib import Path

from check_onenode_digest import check
from work_cost_report import production_attempts


def read_json(path):
    return json.loads(path.read_text())


def geom(values):
    return statistics.geometric_mean(values)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign', type=Path)
    ap.add_argument('gap_job')
    ap.add_argument('feature_job')
    ap.add_argument('trace_job')
    ap.add_argument('--archive', type=Path)
    args = ap.parse_args()
    root = args.campaign
    gap = read_json(root / f'probe-{args.gap_job}/summary.json')
    features = read_json(root / f'features-{args.feature_job}/summary.json')
    trace_root = root / f'traces/baseline-{args.trace_job}'
    trace_protocol = read_json(Path(__file__).with_name('delta-road-gap-trace-protocol.json'))
    graph_root = Path(trace_protocol['graphs_root'])
    assert gap['validated_solves'] == 120 and not gap['invalid']
    gap_rows = [json.loads(line) for line in (root / f'probe-{args.gap_job}/runs.jsonl').read_text().splitlines()]
    assert len(gap_rows) == 120 and all(row['valid'] for row in gap_rows)
    feature_rows = [json.loads(line) for line in (root / f'features-{args.feature_job}/runs.jsonl').read_text().splitlines()]
    assert features['valid_solves'] == len(feature_rows) == 64
    assert all(row['digest_valid'] for row in feature_rows)
    combined = dict(jobs=dict(gap=args.gap_job, features=args.feature_job, trace=args.trace_job),
                    validated_solves=120 + 64 + 8, graphs={},
                    scope='One-node attribution. The Wasp feature modes are standalone single-feature variants; Projections PE-time shares are not causal wall-time fractions.')
    for name, result in gap['results'].items():
        with (graph_root / f'{name}.wsg').open('rb') as stream:
            header = stream.read(17)
        edges, vertices = struct.unpack_from('<qq', header, 1)
        item = dict(vertices=vertices, edges=edges,
                    wasp_selection={key: result['selected_wasp'][key] for key in ('threads', 'delta')},
                    acic_speedup_over_wasp=result['speedup_geomean'],
                    sources={source: dict(
                        acic_seconds=row['acic']['seconds'],
                        wasp_seconds=row['wasp']['seconds'],
                        speedup_over_wasp=row['speedup_over_wasp'],
                        acic_rounds=row['acic']['rounds'],
                        acic_edge_attempts=row['acic']['edge_attempts'])
                        for source, row in result['sources'].items()})
        if name in features['manifest']['graphs']:
            profile = features['manifest']['graphs'][name]
            item.update(vertices=profile['vertices'], edges=profile['edges'],
                        average_degree=profile['average_degree'],
                        leaf_fraction=profile['leaf_fraction'])
            sources = features['results'][name]
            item['wasp_feature_speedups'] = {
                label: geom(row[key] for row in sources.values())
                for label, key in (('full_over_noopt', 'speedup_full_over_noopt'),
                                   ('pull_over_noopt', 'speedup_pull_over_noopt'),
                                   ('leaves_over_noopt', 'speedup_leaves_over_noopt'))}
            item['wasp_feature_medians'] = {source: row['medians']
                                            for source, row in sources.items()}
        combined['graphs'][name] = item
    for name, cfg in trace_protocol['graphs'].items():
        source = cfg['source']
        graph = combined['graphs'][name]
        assert graph['vertices'] is not None
        trace_dir = trace_root / name
        projection = read_json(trace_dir / 'projections.json')
        assert projection['pes'] == 112 and projection['processes'] == 16
        logs = list(trace_dir.glob(f'acic_base_gap_prj_{args.trace_job}.*.log.gz'))
        assert len(logs) == 112, (name, len(logs))
        runs = []
        for label in ('warmup', 'plain-before', 'traced', 'plain-after'):
            log = trace_dir / f'{label}.out'
            check(graph_root / f'{name}.reference.txt', source, log)
            text = log.read_text()
            assert 'Starting Reconverse with 16 processes, 112 PEs' in text
            assert 'Using the original scheduler (+old-scheduler)' in text
            assert 'Projections log flushed to disk' not in text
            runs.append(dict(label=label,
                             seconds=float(re.search(r'Compute time: ([0-9.eE+-]+)', text)[1]),
                             rounds=text.count('Updates: created:'),
                             edge_attempts=production_attempts(text, graph['vertices'])))
        plain = [row['seconds'] for row in runs if row['label'].startswith('plain-')]
        traced = next(row for row in runs if row['label'] == 'traced')
        time = {row['activity']: row for row in projection['time']}
        heap_latency = next(row for row in projection['latency']
                            if row['ep'] == 'SsspChares::process_heap'
                            and row['locality'] == 'same process')
        graph['trace'] = dict(source=source, runs=runs, pe_window_s=projection['window_s_mean'],
                              plain_mean_s=statistics.mean(plain),
                              overhead_ratio=traced['seconds']/statistics.mean(plain),
                              trace_log_count=len(logs),
                              trace_log_bytes=sum(path.stat().st_size for path in logs),
                              idle_share=time['idle']['share'],
                              heap_entry_share=time['SsspChares::process_heap']['share'],
                              heap_entry_calls=time['SsspChares::process_heap']['calls'],
                              threshold_share=time['SsspChares::current_thresholds']['share'],
                              threshold_calls=time['SsspChares::current_thresholds']['calls'],
                              untraced_share=time['untraced']['share'],
                              heap_send_to_execute_p90_ms=heap_latency['p50_p90_p99_max_ms'][1])
    lines = ['# Delta one-node road gap attribution', '',
             f"Jobs: gap {args.gap_job}, Wasp features {args.feature_job}, ACIC traces {args.trace_job}; {combined['validated_solves']} independently validated solves.",
             '', '## Same-node ACIC/Wasp comparison', '',
             '| Graph | Edges | ACIC/Wasp speedup | ACIC seconds, two sources | Wasp seconds, two sources | ACIC rounds |',
             '|---|---:|---:|---|---|---|']
    for name, graph in combined['graphs'].items():
        values = list(graph['sources'].values())
        acic_times = ' / '.join(f"{v['acic_seconds']:.3f}" for v in values)
        wasp_times = ' / '.join(f"{v['wasp_seconds']:.3f}" for v in values)
        rounds = ' / '.join(f"{v['acic_rounds']:.0f}" for v in values)
        lines.append(f"| {name} | {graph['edges']:,} | {graph['acic_speedup_over_wasp']:.3f}× | "
                     f"{acic_times} | {wasp_times} | {rounds} |")
    lines += ['', '## Wasp artifact feature variants', '',
              '| Graph | Degree-one vertices | Full/no-opt | Pull-only/no-opt | Leaves-only/no-opt |',
              '|---|---:|---:|---:|---:|']
    for name in ('road-usa-z', 'mesh24-z'):
        graph = combined['graphs'][name]
        v = graph['wasp_feature_speedups']
        lines.append(f"| {name} | {graph['leaf_fraction']:.1%} | {v['full_over_noopt']:.2f}× | {v['pull_over_noopt']:.2f}× | {v['leaves_over_noopt']:.2f}× |")
    lines += ['', '## Matched ACIC Projections', '',
              '| Graph | Idle PE time | `process_heap` PE time | Heap calls | Threshold calls | Heap p90 wait | Trace/control |',
              '|---|---:|---:|---:|---:|---:|---:|']
    for name in ('road-usa-z', 'mesh24-z'):
        trace = combined['graphs'][name]['trace']
        lines.append(f"| {name} | {trace['idle_share']:.1%} | {trace['heap_entry_share']:.1%} | {trace['heap_entry_calls']:,} | {trace['threshold_calls']:,} | {trace['heap_send_to_execute_p90_ms']:.3f} ms | {trace['overhead_ratio']:.3f}× |")
    lines += ['', 'The quarter-weight road preserves topology and scales ACIC width 131072→32768 and Wasp delta 32768→8192. Its ACIC/Wasp ratio barely changes. The similar-size mesh is also far from Wasp on one node, but ACIC road has more rounds and idle time despite fewer edge attempts. Wasp pull helps both graphs; leaf pruning alone helps neither in this screen. A single-feature Wasp variant is not a direct ablation of its full implementation.',
              '', 'Raw outputs, logs, manifests and full Projections reports are under this campaign on /work/hdd.']
    (root / 'report.md').write_text('\n'.join(lines)+'\n')
    (root / 'summary.json').write_text(json.dumps(combined, indent=2)+'\n')
    if args.archive:
        args.archive.write_text(json.dumps(combined, indent=2)+'\n')
    print('COMPLETE', root / 'report.md', combined['validated_solves'])


if __name__ == '__main__':
    main()
