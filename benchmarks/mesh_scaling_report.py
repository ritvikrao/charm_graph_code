#!/usr/bin/env python3
"""Strong scaling and queue/batch/slice ablation from audited allocations.

Reads the check_priority_runs.py audit of each allocation of
frontier-mesh-scaling-variants.json, the raw source logs for round counts, the
work-cost summaries, and the one-node GAPBS reference on the same held-out
sources. Allocations stay separate; a source's value is its median over
repetitions, and an arm's value is the median over sources. Each prediction
recorded in the configuration before submission is checked here as written.
"""
import argparse
import json
from pathlib import Path
import re
from statistics import median

NODES = (1, 2, 4, 8, 16)
ABLATION = ('n16_local', 'n16_b1', 'n16_b8')


def per_source(values):
    return median(values.values())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('campaign', type=Path)
    ap.add_argument('audits', nargs='+', type=Path)
    ap.add_argument('--gap', type=Path, required=True, help='GAPBS external-*.jsonl with held-out rows')
    ap.add_argument('--graph', default='mesh26-z')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()

    gap_rows = [json.loads(l) for l in args.gap.read_text().splitlines()]
    gap_rows = [r for r in gap_rows if r['graph'] == args.graph and r['phase'] == 'external'
                and r['config']['engine'] == 'gap' and r['valid']]
    gap = {str(s): median(r['seconds'] for r in gap_rows if r['source'] == s)
           for s in {r['source'] for r in gap_rows}}

    result = dict(graph=args.graph, gap_seconds=gap, allocations=[])
    for path in args.audits:
        audit = json.loads(path.read_text())
        allocation = next(a for a in audit['allocations'] if a['graph'] == args.graph)
        job, summary = allocation['job'], allocation['summary']
        directory = next((args.campaign/'logs').glob(f'AB-{args.graph}-*n-{job}'))
        sources = sorted(summary['n16_s8']['sources'])

        def metric(label, name):
            return {s: summary[label]['sources'][s][name] for s in sources}

        # Rounds: "Number of reductions" in each timed source log.
        rows = [json.loads(l) for l in (directory/'runs.jsonl').read_text().splitlines()]
        rounds = {}
        for label in summary:
            rounds[label] = {}
            for s in sources:
                values = []
                for r in rows:
                    if r['variant'] == label and str(r['source']) == s and r['rep'] >= 0:
                        text = (directory/f"{label}-s{r['source_index']}-r{r['rep']}.out").read_text()
                        values.append(int(re.search(r'^Number of reductions: (\d+)', text, re.M)[1]))
                rounds[label][s] = median(values)
        # Offered-update topology and time shares from the work-cost build.
        cost = json.loads((directory/'work-cost.json').read_text())
        topology = {}
        for n in NODES:
            cells = [c for c in cost if c['variant'] == f'n{n}_s8_cost' and c['rep'] >= 0]
            topology[n] = {s: median(c['counters']['inter_node'] / c['counters']['edge_attempts']
                                     for c in cells if str(c['source']) == s) for s in sources}
            topology[n] = dict(inter_node_fraction=per_source(topology[n]),
                intra_node_fraction=median(c['counters']['intra_node'] / c['counters']['edge_attempts'] for c in cells),
                idle_share=median(c['comm']['idle_share'] for c in cells),
                send_share=median(c['comm']['send_share'] for c in cells))

        seconds = {n: metric(f'n{n}_s8', 'seconds') for n in NODES}
        scaling = []
        for n in NODES:
            speedup = {s: seconds[1][s] / seconds[n][s] for s in sources}
            scaling.append(dict(nodes=n, pes=56*n, seconds=per_source(seconds[n]),
                attempts_per_edge=per_source(metric(f'n{n}_s8', 'attempts_per_edge')),
                rounds=per_source(rounds[f'n{n}_s8']), speedup=per_source(speedup),
                efficiency=per_source(speedup) / n,
                gap_ratio=per_source({s: seconds[n][s] / gap[s] for s in sources}),
                **topology[n]))
        ablation = []
        for label in ('n16_s8', 'n16_s8_control') + ABLATION:
            t = metric(label, 'seconds')
            ablation.append(dict(arm=label, seconds=per_source(t),
                attempts_per_edge=per_source(metric(label, 'attempts_per_edge')),
                rounds=per_source(rounds[label]),
                vs_candidate={s: t[s] / seconds[16][s] for s in sources},
                vs_candidate_median=per_source({s: t[s] / seconds[16][s] for s in sources})))

        # The predictions recorded in the configuration, checked as written.
        def within(label, lo, hi, name='seconds'):
            return lo <= per_source(metric(label, name)) <= hi

        def every(a, b, factor=1.0):  # a slower than b by at least factor on every source
            ta, tb = metric(a, 'seconds'), metric(b, 'seconds')
            return all(ta[s] >= factor * tb[s] for s in sources)
        ape = lambda label: per_source(metric(label, 'attempts_per_edge'))
        control = metric('n16_s8_control', 'seconds')
        speedup16 = per_source({s: seconds[1][s] / seconds[16][s] for s in sources})
        predictions = {
            'n1_s8 1.50-1.80 s': within('n1_s8', 1.50, 1.80),
            'n1_s8 1.25-1.40 attempts/edge': within('n1_s8', 1.25, 1.40, 'attempts_per_edge'),
            'n2_s8 between n1 and n4 on every source': every('n1_s8', 'n2_s8') and every('n2_s8', 'n4_s8'),
            'n4_s8 between n2 and n8 on every source': every('n2_s8', 'n4_s8') and every('n4_s8', 'n8_s8'),
            'n8_s8 0.34-0.38 s': within('n8_s8', 0.34, 0.38),
            'n8_s8 1.85-2.05 attempts/edge': within('n8_s8', 1.85, 2.05, 'attempts_per_edge'),
            'n16_s8 0.25-0.27 s': within('n16_s8', 0.25, 0.27),
            'n16_s8 2.7-2.9 attempts/edge': within('n16_s8', 2.7, 2.9, 'attempts_per_edge'),
            'time falls at every doubling on every source': all(every(f'n{a}_s8', f'n{b}_s8')
                for a, b in zip(NODES, NODES[1:])),
            'n1/n16 speedup 5.0-7.5x': 5.0 <= speedup16 <= 7.5,
            'attempts/edge do not fall with nodes': all(ape(f'n{a}_s8') <= ape(f'n{b}_s8')
                for a, b in zip(NODES, NODES[1:])),
            'control within 2% on every source': all(abs(control[s] / seconds[16][s] - 1) <= 0.02 for s in sources),
            'n16_b8 0.37-0.42 s': within('n16_b8', 0.37, 0.42),
            'n16_b8 5.0-5.4 attempts/edge': within('n16_b8', 5.0, 5.4, 'attempts_per_edge'),
            'n16_b8 >= 1.3x n16_s8 on every source': every('n16_b8', 'n16_s8', 1.3),
            'n16_b1 slower than n16_b8 on every source': every('n16_b1', 'n16_b8'),
            'n16_b1 attempts/edge not above n16_b8': ape('n16_b1') <= ape('n16_b8'),
            'n16_local more attempts/edge than n16_b8': ape('n16_local') > ape('n16_b8'),
            'n16_local slower than n16_s8 on every source': every('n16_local', 'n16_s8'),
            'inter-node share rises with nodes': all(topology[a]['inter_node_fraction'] <= topology[b]['inter_node_fraction']
                for a, b in zip(NODES, NODES[1:])),
        }
        result['allocations'].append(dict(job=job, sources=sources, scaling=scaling, ablation=ablation,
                                          predictions=predictions, audit=str(path)))

        print(f'== allocation {job}')
        print('nodes  PEs   seconds  speedup  eff   attempts/edge  rounds  GAPBS ratio  inter-node  idle')
        for r in scaling:
            print('%5d %5d  %7.3f  %6.2fx  %4.0f%%  %13.2f  %6.0f  %11.2f  %9.1f%%  %4.2f' % (
                r['nodes'], r['pes'], r['seconds'], r['speedup'], 100*r['efficiency'], r['attempts_per_edge'],
                r['rounds'], r['gap_ratio'], 100*r['inter_node_fraction'], r['idle_share']))
        print('arm              seconds  vs cand  attempts/edge  rounds')
        for r in ablation:
            print('%-15s  %7.3f  %6.2fx  %13.2f  %6.0f' % (r['arm'], r['seconds'], r['vs_candidate_median'],
                                                           r['attempts_per_edge'], r['rounds']))
        for k, v in predictions.items():
            print(('MET    ' if v else 'MISSED ') + k)
    args.output.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
