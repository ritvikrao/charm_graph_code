#!/usr/bin/env python3
"""Reaudit a completed Delta road-eu comparison and write its compact record."""
import argparse
import json
from pathlib import Path
import statistics

from check_onenode_digest import check


def read(path):
    return json.loads(path.read_text())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign', type=Path)
    ap.add_argument('job')
    ap.add_argument('--archive', type=Path)
    args = ap.parse_args()
    out = args.campaign / f'compare-{args.job}'
    summary = read(out / 'summary.json')
    protocol = summary['manifest']['protocol']
    reference = Path(protocol['graphs_root']) / f"{protocol['graph']}.reference.txt"
    rows = [json.loads(line) for line in (out / 'runs.jsonl').read_text().splitlines()]
    assert summary['manifest']['job'] == args.job
    assert summary['valid_solves'] == len(rows)
    assert len({row['index'] for row in rows}) == len(rows)
    assert all(row['valid'] and row['returncode'] == 0 for row in rows)
    for row in rows:
        check(reference, row['source'], Path(row['log']))
    selection = summary['selection']
    train = selection['training_sources']
    assert len(train) == protocol['training_sources']
    assert selection['acic_width'] in protocol['acic_widths']
    assert selection['wasp_threads'] in protocol['wasp_threads']
    assert selection['wasp_delta'] in protocol['wasp_deltas']
    assert selection['acic_width'] == int(min(
        selection['scores']['acic'], key=selection['scores']['acic'].get))
    key = min(selection['scores']['wasp'], key=selection['scores']['wasp'].get)
    assert key == f"{selection['wasp_threads']}x{selection['wasp_delta']}"
    expected_train = len(train) * protocol['training_repetitions'] * (
        len(protocol['acic_widths']) +
        len(protocol['wasp_threads']) * len(protocol['wasp_deltas']))
    expected_test = protocol['test_sources'] * (
        protocol['test_warmups'] + protocol['test_repetitions']) * len(protocol['test_arms'])
    assert len(rows) == expected_train + expected_test
    assert len(summary['results']) == protocol['test_sources']
    assert not set(train) & set(summary['results'])
    for source, result in summary['results'].items():
        for arm in protocol['test_arms']:
            values = [row['seconds'] for row in rows if row['phase'] == 'test'
                      and row['source'] == source and row['arm'] == arm]
            assert len(values) == protocol['test_repetitions']
            assert abs(statistics.median(values) - result[arm]['seconds']) < 1e-12
    archive = dict(job=args.job, host=summary['manifest']['host'],
                   protocol=protocol, graph=summary['manifest']['graph'],
                   binaries=summary['manifest']['binaries'],
                   selection=selection, valid_solves=len(rows),
                   results=summary['results'],
                   speedup_geomean=summary['speedup_geomean'],
                   raw_directory=str(out))
    lines = ['# Delta one-node road-eu ACIC/Wasp comparison', '',
             f"Job {args.job} on {archive['host']}; {len(rows)} digest-checked solves.", '',
             f"ACIC original heap width {selection['acic_width']}; Wasp "
             f"{selection['wasp_threads']} threads, Δ {selection['wasp_delta']}.", '',
             '| Test source | ACIC (s) | Duplicate ACIC (s) | Wasp (s) | ACIC/Wasp speedup | ACIC rounds | ACIC edge attempts |',
             '|---:|---:|---:|---:|---:|---:|---:|']
    for source, result in summary['results'].items():
        lines.append(f"| {source} | {result['acic']['seconds']:.6f} | "
                     f"{result['acic_control']['seconds']:.6f} | "
                     f"{result['wasp']['seconds']:.6f} | "
                     f"{result['speedup_over_wasp']:.3f}× | "
                     f"{result['acic']['rounds']:.0f} | "
                     f"{result['acic']['edge_attempts']:.0f} |")
    lines += ['', f"Geometric-mean ACIC/Wasp speedup: {archive['speedup_geomean']:.3f}×.",
              '', 'Training scores, every command, complete logs, hashes and per-run timings are in the raw directory.',
              'Both engines use the same exclusive Delta node; timings exclude input loading.',
              'The result compares different road graphs and does not isolate graph size.']
    (args.campaign / 'report.md').write_text('\n'.join(lines) + '\n')
    if args.archive:
        args.archive.write_text(json.dumps(archive, indent=2) + '\n')
    print('COMPLETE', args.campaign / 'report.md', len(rows))


if __name__ == '__main__':
    main()
