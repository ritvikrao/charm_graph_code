#!/usr/bin/env python3
"""Classify Frontier launches as fast or slow mode, by a rule fixed in advance.

Frontier multi-node launches of the scale-free graphs run in one of two modes
chosen at start-up (design/current-state.md, mechanism 7): the slow mode does
the same work and differs only in the tail. This script separates the modes
without using the quantity being compared (solve time), then reports each
arm's fast-mode time and its slow-launch rate side by side.

Rule (recorded 2026-09-23 before the first run that uses it):

1. A solve's tail is its compute time minus the time at which it had created
   95% of its updates (tail_split.split).
2. Arms are grouped into configurations: identical binary, flags and layout.
   A repeated control shares its arm's configuration.
3. Within a configuration, a solve's score is log(tail) minus the median
   log(tail) of that source's timed solves; a launch's score is the mean of
   its solves' scores. The mode is a property of the launch, not the solve.
4. Split the configuration's launch scores into two classes at the cut that
   maximizes between-class variance (Otsu). Two modes exist only if the
   classes' median scores differ by at least log(1.5); otherwise every launch
   is one mode, reported as fast.
5. Per arm and source: fast-mode time is the median over the arm's fast
   launches; the arm's value is the median over sources. An arm with no fast
   launch has no fast-mode value; nothing is substituted. The slow-launch
   count, the unconditional median and the mean over all launches are always
   reported beside it.
"""
import argparse
import json
import math
from pathlib import Path
from statistics import mean, median

from tail_split import split

MIN_TAIL_RATIO = 1.5


def otsu(scores):
    ordered = sorted(scores)
    best, cut = -1.0, 1
    for i in range(1, len(ordered)):
        low, high = ordered[:i], ordered[i:]
        weight = len(low) * len(high) * (mean(high) - mean(low)) ** 2
        if weight > best:
            best, cut = weight, i
    return ordered[cut - 1], ordered[cut]


def classify(directory):
    manifest = json.loads((directory/'manifest.json').read_text())
    rows = [json.loads(l) for l in (directory/'runs.jsonl').read_text().splitlines()]
    rows = [r for r in rows if r['rep'] >= 0]
    config = {v['label']: json.dumps([v['binary'], v.get('flags', []), v['nodes'], v['rpn'], v['workers']])
              for v in manifest['variants']}
    solves = {}
    for r in rows:
        text = (directory/f"{r['variant']}-s{r['source_index']}-r{r['rep']}.out").read_text()
        tail = split(text, r['seconds'])['tail']
        solves[r['variant'], r['rep'], str(r['source'])] = dict(seconds=r['seconds'], log_tail=math.log(tail))
    modes, configurations = {}, {}
    for key in sorted(set(config.values())):
        labels = [l for l, c in config.items() if c == key]
        mine = {k: v for k, v in solves.items() if k[0] in labels}
        centre = {s: median(v['log_tail'] for k, v in mine.items() if k[2] == s) for s in {k[2] for k in mine}}
        scores = {}
        for (label, rep, source), v in mine.items():
            scores.setdefault((label, rep), []).append(v['log_tail'] - centre[source])
        scores = {k: mean(v) for k, v in scores.items()}
        below, above = otsu(scores.values()) if len(scores) > 1 else (math.inf, math.inf)
        low = [s for s in scores.values() if s <= below]
        high = [s for s in scores.values() if s >= above]
        bimodal = bool(high) and math.exp(median(high) - median(low)) >= MIN_TAIL_RATIO
        for k, s in scores.items():
            modes[k] = 'slow' if bimodal and s >= above else 'fast'
        configurations[key] = dict(arms=labels, launches=len(scores), bimodal=bimodal,
            tail_ratio=math.exp(median(high) - median(low)) if high else 1.0,
            slow_launches=sum(modes[k] == 'slow' for k in scores))
    arms = {}
    for label in config:
        sources = sorted({k[2] for k in solves if k[0] == label}, key=int)
        reps = sorted({k[1] for k in solves if k[0] == label})
        per_source = {}
        for s in sources:
            fast = [solves[label, r, s]['seconds'] for r in reps if modes[label, r] == 'fast']
            slow = [solves[label, r, s]['seconds'] for r in reps if modes[label, r] == 'slow']
            every = [solves[label, r, s]['seconds'] for r in reps]
            per_source[s] = dict(fast=median(fast) if fast else None, slow=median(slow) if slow else None,
                                 median=median(every), mean=mean(every))
        fast_values = [v['fast'] for v in per_source.values()]
        arms[label] = dict(configuration=config[label], launches=len(reps),
            slow_launches=sum(modes[label, r] == 'slow' for r in reps),
            fast=median(fast_values) if None not in fast_values else None,
            median=median(v['median'] for v in per_source.values()),
            mean=median(v['mean'] for v in per_source.values()),
            sources=per_source, modes={str(r): modes[label, r] for r in reps})
    return dict(directory=directory.name, rule_min_tail_ratio=MIN_TAIL_RATIO,
                configurations=configurations, arms=arms)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('directories', nargs='+', type=Path)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    results = [classify(d) for d in args.directories]
    for r in results:
        print('==', r['directory'])
        for c in r['configurations'].values():
            print('  %-40s launches %3d  %s  tail x%.2f  slow %d' % (','.join(c['arms'])[:40], c['launches'],
                  'two modes' if c['bimodal'] else 'one mode ', c['tail_ratio'], c['slow_launches']))
        for label, a in r['arms'].items():
            print('  %-24s fast %s  slow %d/%d  median %.3f  mean %.3f' % (label,
                  '%.3f' % a['fast'] if a['fast'] is not None else '  -  ', a['slow_launches'], a['launches'],
                  a['median'], a['mean']))
    if args.output:
        args.output.write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
