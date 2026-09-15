#!/usr/bin/env python3
"""Paired arm tables for run.py campaigns, with failures and floors beside them.

    report_arms.py LOGS --pattern 'width-*.jsonl' --phase width \\
        --baseline logv-frozen --control control OUTPUT

Every arm in an allocation is paired against the baseline by (graph, source,
rep). A cell reports the paired median speedup (baseline time / arm time, so
above 1 favours the arm), how many pairs the arm won, its outcome counts, and a
verdict against the resolution floor measured *in that allocation* -- the
control arm, which is the baseline under another name. See outcomes.py for why
both columns are there and why neither is optional.

An allocation is one Slurm job. Two jobs at the same node count are two
allocations with two floors, which is the point: the floor is a property of
the allocation, not of the node count.
"""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path

from outcomes import (OUTCOMES, Floors, classify, counts, failures, outcome_text,
                      paired_ratios, ratio_text, summarize_ratios)


def load(logs, pattern, phase):
    records = []
    for path in sorted(Path(logs).glob(pattern)):
        for line in path.read_text().splitlines():
            if line.strip():
                r = json.loads(line)
                if r.get('phase') == phase:
                    records.append(r)
    return records


def build(records, baseline, control):
    by_cell = defaultdict(list)          # (job, graph, arm) -> records
    allocations = {}
    for r in records:
        job = str(r['job'])
        allocations[job] = (r['nodes'], r['workers'], r['config'].get('rpn', 1))
        by_cell[(job, r['graph'], r['config']['name'])].append(r)
    floors = Floors()
    cells = []
    for job in sorted(list(allocations), key=lambda j: (allocations[j], j)):
        graphs = sorted({g for (j, g, _) in by_cell if j == job})
        arms = sorted({a for (j, _, a) in by_cell if j == job})
        if baseline not in arms:
            # A completion or replay job can carry a subset of arms. Nothing in
            # it can be paired, so it is named rather than silently dropped.
            print(f'skipping job {job}: no {baseline!r} arm (arms: {", ".join(arms)})')
            continue
        times = {}
        for (j, g, a), runs in by_cell.items():
            if j == job:
                times[(g, a)] = {(r['source'], r.get('rep', 0)): r['seconds']
                                 for r in runs if classify(r) == 'ok'}
        for g in graphs:
            if control in arms:
                floors.add(job, g, summarize_ratios(
                    paired_ratios(times.get((g, baseline), {}), times.get((g, control), {}))))
        for g in graphs:
            base_counts = counts(by_cell.get((job, g, baseline), []))
            for a in arms:
                runs = by_cell.get((job, g, a), [])
                if not runs:
                    continue
                c = counts(runs)
                summary = None if a == baseline else summarize_ratios(
                    paired_ratios(times.get((g, baseline), {}), times.get((g, a), {})))
                clean = failures(c) == 0 and failures(base_counts) == 0
                cells.append(dict(job=job, allocation=allocations[job], graph=g, arm=a,
                                  counts=c, summary=summary,
                                  verdict='baseline' if a == baseline else floors.verdict(job, g, summary, clean)))
    return allocations, floors, cells


MARK = {'clears': '', 'own floor only': ' ~', 'within floor': ' ·',
        'survivors only': ' †', 'no pairs': '', 'no floor': ' ?', 'baseline': ''}


def render(allocations, floors, cells, baseline, control):
    out = ['Speedups are paired medians of baseline / arm time over (source, rep); '
           'above 1 favours the arm. `won` counts pairs the arm was faster in. '
           f'The floor is `{control}` against `{baseline}`, the same configuration run twice, '
           'measured in the same allocation.',
           '',
           'Marks: `·` within the allocation floor and within the graph\'s own floor -- not a result; '
           '`~` clears only the graph\'s own floor; `†` the cell or its baseline did not always finish, '
           'so the ratio is over survivors and is not a speedup; `?` no control pairs for this graph.',
           '']
    for job, (nodes, workers, rpn) in sorted(allocations.items(), key=lambda x: (x[1], x[0])):
        here = [c for c in cells if c['job'] == job]
        arms = [a for a in dict.fromkeys(c['arm'] for c in here) if a != baseline]
        arms.sort(key=lambda a: (a == control, a))
        floor = floors.allocation(job)
        out.append(f'### Job {job}: {nodes} node(s), {workers} workers/node, {rpn} process(es)/node')
        out.append('')
        out.append(f'Allocation floor (worst graph): **{ratio_text(floor)}**' if floor else 'Allocation floor: none (no control arm)')
        out.append('')
        head = ['graph', f'`{baseline}` failed'] + [f'`{a}`' for a in arms] + ['graph floor']
        out.append('| ' + ' | '.join(head) + ' |')
        out.append('|---|---:|' + '---|' * len(arms) + '---:|')
        for g in sorted({c['graph'] for c in here}):
            row = {c['arm']: c for c in here if c['graph'] == g}
            base = row.get(baseline)
            cols = [g, outcome_text(base['counts']) if base else '—']
            for a in arms:
                c = row.get(a)
                if not c:
                    cols.append('—')
                    continue
                s = c['summary']
                text = (f"{ratio_text(s['median'])} {s['won']}/{s['pairs']}" if s else 'no pairs')
                text += MARK[c['verdict']]
                if failures(c['counts']):
                    text += f"; failed {outcome_text(c['counts'])}"
                cols.append(text)
            own = floors.graph(job, g)
            cols.append(ratio_text(own) if own else '—')
            out.append('| ' + ' | '.join(cols) + ' |')
        out.append('')
    # The hang table on its own, because a hang rate is a result in its own
    # right and should not have to be read out of the footnotes of a speedup.
    out.append('### Failures by arm and allocation')
    out.append('')
    out.append('| job | nodes | arm | attempted | hung | wrong | crashed | rescued | failure rate |')
    out.append('|---|---:|---|---:|---:|---:|---:|---:|---:|')
    totals = defaultdict(lambda: defaultdict(int))
    for c in cells:
        for k, v in c['counts'].items():
            totals[(c['job'], c['arm'])][k] += v
    for (job, arm), t in sorted(totals.items(), key=lambda x: (allocations[x[0][0]], x[0])):
        rate = failures(t) / t['attempted'] if t['attempted'] else 0
        out.append(f"| {job} | {allocations[job][0]} | {arm} | {t['attempted']} | {t['hang']} | "
                   f"{t['wrong']} | {t['crash']} | {t['rescued']} | {100 * rate:.1f}% |")
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('logs', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--pattern', required=True, help="glob under LOGS, e.g. 'width-*.jsonl'")
    parser.add_argument('--phase', required=True, help='record phase holding the timed runs')
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--control', required=True,
                        help='the baseline run under another name; a campaign without one has no floor')
    args = parser.parse_args()
    records = load(args.logs, args.pattern, args.phase)
    if not records:
        raise SystemExit(f'no {args.phase!r} records in {args.logs}/{args.pattern}')
    allocations, floors, cells = build(records, args.baseline, args.control)
    allocations = {j: a for j, a in allocations.items() if any(c['job'] == j for c in cells)}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'arms.md').write_text(render(allocations, floors, cells, args.baseline, args.control))
    with (args.output / 'arms.csv').open('w', newline='') as f:
        fields = ['job', 'nodes', 'workers', 'rpn', 'graph', 'arm', 'attempted', *OUTCOMES, 'rescued',
                  'median', 'geomean', 'ci_low', 'ci_high', 'won', 'pairs',
                  'graph_floor', 'allocation_floor', 'verdict']
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for c in cells:
            nodes, workers, rpn = c['allocation']
            writer.writerow(dict(job=c['job'], nodes=nodes, workers=workers, rpn=rpn, graph=c['graph'],
                                 arm=c['arm'], **{k: c['counts'][k] for k in ['attempted', *OUTCOMES, 'rescued']},
                                 **(c['summary'] or {}), graph_floor=floors.graph(c['job'], c['graph']),
                                 allocation_floor=floors.allocation(c['job']), verdict=c['verdict']))
    print(f"{len(records)} records, {len(allocations)} allocation(s), {len(cells)} cells -> {args.output}")


if __name__ == '__main__':
    main()
