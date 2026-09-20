#!/usr/bin/env python3
"""Check lightweight R0 counters against raw runs and independent references."""
import argparse
import json
import math
from pathlib import Path
import re
from check_onenode_digest import check
from onenode_report import fields


def production_attempts(text, vertices):
    """Recover exact scans from the existing retirement ledger, no new timers.

    Valid for the R0 non-lazy path: every scan is filtered, absorbed, folded,
    or retired at its receiver. Wasted's printed value subtracts all V, and
    the injected source accounts for one retirement without an edge scan.
    """
    if 'Lazy heavy:' in text:
        raise ValueError('production ledger formula requires lazy-heavy off')
    def value(label):
        found = re.findall(re.escape(label) + r': (-?\d+)', text)
        if len(found) != 1:
            raise ValueError(f'expected one {label}')
        return int(found[0])
    return vertices - 1 + sum(value(label) for label in
        ['Wasted updates', 'Send-filtered updates', 'Absorbed updates', 'Batch-folded updates'])


def counters(text):
    lines = re.findall(r'WORK_COST ([^\r\n]*)', text)
    if len(lines) != 1:
        raise ValueError(f'expected one WORK_COST record, found {len(lines)}')
    result = {}
    for item in lines[0].split():
        key, value = item.split('=')
        if key in result or not re.fullmatch(r'\d+', value):
            raise ValueError('invalid or duplicate work counter')
        result[key] = int(value)
    if result['queue_pushes'] != result['queue_pops']:
        raise ValueError('queued vertex work did not drain')
    if result['expansions'] + result['stale_pops'] != result['queue_pops']:
        raise ValueError('expansion/stale accounting mismatch')
    if result['cas_failures'] > result['cas_attempts']:
        raise ValueError('CAS failures exceed attempts')
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    args = ap.parse_args()
    root = args.directory.parents[1]
    rows = [json.loads(line) for line in (args.directory / 'runs.jsonl').read_text().splitlines()]
    manifest = json.loads((args.directory / 'manifest.json').read_text())
    expected_labels = {v['label'] for v in manifest['variants'] if v.get('work_cost')}
    output = []
    for row in rows:
        log = args.directory / f"{row['variant']}-s{row['source_index']}-r{row['rep']}.out"
        text = log.read_text()
        if row['variant'] in expected_labels and 'WORK_COST ' not in text:
            raise ValueError(f'{log}: missing expected counters')
        if 'WORK_COST ' not in text:
            continue
        reference = root / 'graphs' / f"{row['graph']}.reference.txt"
        check(reference, row['source'], log)
        ref = next(line.split() for line in reference.read_text().splitlines()
                   if line.split() and line.split()[0] == str(row['source']))
        c = counters(text)
        meta = (root / 'graphs' / f"{row['graph']}.meta").read_text()
        vertices = int(re.search(r'vertices=(\d+)', meta)[1])
        if c['edge_attempts'] != production_attempts(text, vertices):
            raise ValueError(f'{log}: edge scans disagree with existing retirement ledger')
        comm = fields(text, 'COMM_SHARE')
        times = re.findall(r'Compute time: ([\d.eE+-]+)', text)
        if (len(times) != 1 or not row['valid'] or float(times[0]) != row['seconds']
                or not math.isfinite(row['seconds']) or row['seconds'] <= 0):
            raise ValueError(f'{log}: invalid or inconsistent solve time')
        output.append(dict(**row, counters=c, comm=comm,
            reachable_vertices=int(ref[4]), reachable_arcs=int(ref[7]),
            attempts_per_arc=c['edge_attempts'] / int(ref[7]),
            expansions_per_vertex=c['expansions'] / int(ref[4]),
            cpu_ns_per_attempt=c['cpu_ns'] / c['edge_attempts'],
            work_ns_per_attempt=1e9 * (comm['work_seconds'] - comm['work_send_seconds']) / c['edge_attempts'],
            queue_estimated_seconds=(c['push_ticks'] + c['pop_ticks']) * c['sample_period']
                * comm['window_pe_seconds'] / fields(text, 'WORK_CLOCK')['window_ticks']))
    if not output:
        raise ValueError('no diagnostic records')
    if expected_labels:
        expected = {(v, s, r) for v in expected_labels for s in range(manifest['sources'])
                    for r in range(-1, manifest['reps'])}
        keys = [(r['variant'], r['source_index'], r['rep']) for r in output]
        if len(keys) != len(set(keys)) or set(keys) != expected:
            raise ValueError('missing, duplicate or extra diagnostic cells')
    (args.directory / 'work-cost.json').write_text(json.dumps(output, indent=2) + '\n')
    print(f'WORK COST PASS {len(output)} diagnostic solves: {args.directory}')


if __name__ == '__main__':
    main()
