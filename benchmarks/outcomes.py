#!/usr/bin/env python3
"""What a run's outcome was, and what a comparison can resolve (step 7.6h).

Two failures of campaign design hid the 7.6g deadlock for 1,080 runs, and both
are about reporting rather than the solver:

* A median over valid runs scores "stops sometimes" as a clean win. The 7.6e
  range arm read 1.21x on mesh20 at one node out of a cell that also hung
  twice, because the runs that finished were the runs that extended least.
  So every cell carries its outcome counts, and a ratio taken from a cell with
  any hang is marked as a ratio over survivors, not a speedup.

* The resolution floor is not a constant. `control` is the baseline run a
  second time under another name, so its paired ratio against the baseline is
  measurement noise; it was ~1.00x at one node and 1.16x-1.28x at sixteen. So
  the floor is computed per allocation and printed beside every ratio from it.

Shared by report.py (the 7.5 primary matrix) and report_arms.py (every
arm-based campaign: width, deployment, controller, and what follows).
"""
from collections import defaultdict
import math
import random
import statistics

OUTCOMES = ('ok', 'hang', 'wrong', 'crash')


def timeout_of(record):
    """The --timeout a run was launched with, or None."""
    command = record.get('command') or []
    for flag, value in zip(command, command[1:]):
        if flag == '--timeout':
            try:
                return float(value)
            except ValueError:
                return None
    return None


def classify(record):
    """One of OUTCOMES, or 'skipped' for a slot never run.

    Records written after 7.6h carry `hung`, parsed from the solver's own
    TIMEOUT and PROGRESS_STALL lines. Earlier records do not, and are classified
    from what they do carry: the harness kill (-999), or a compute time that
    reached the run's own --timeout, which is what fast_exit() reports.
    """
    if record.get('skipped'):
        return 'skipped'
    if record.get('valid'):
        return 'ok'
    if record.get('hung') or record.get('returncode') == -999:
        return 'hang'
    limit = timeout_of(record)
    seconds = record.get('seconds')
    if limit and seconds is not None and seconds >= 0.99 * limit:
        return 'hang'
    if record.get('returncode') == 0 and record.get('digest'):
        return 'wrong'
    return 'crash'


def counts(records):
    """Outcome counts over the records of one cell."""
    result = dict.fromkeys(OUTCOMES, 0)
    result['rescued'] = 0
    for r in records:
        outcome = classify(r)
        if outcome in result:
            result[outcome] += 1
        if (r.get('stall_rescues') or 0) > 0:
            result['rescued'] += 1
    result['attempted'] = sum(result[o] for o in OUTCOMES)
    return result


def failures(c):
    return c['hang'] + c['wrong'] + c['crash']


def outcome_text(c):
    """Compact failure column: '0/12', or '3/12 (2 hung, 1 wrong)'."""
    bad = failures(c)
    text = f"{bad}/{c['attempted']}"
    parts = [f'{c[k]} {w}' for k, w in (('hang', 'hung'), ('wrong', 'wrong'), ('crash', 'crashed')) if c[k]]
    if c.get('rescued'):
        parts.append(f"{c['rescued']} rescued")
    return text + (f" ({', '.join(parts)})" if parts else '')


def geometric(values):
    values = list(values)
    return math.exp(statistics.mean(math.log(x) for x in values))


def paired_ratios(base, variant):
    """base/variant for every key both have; >1 means the variant is faster."""
    return [base[k] / variant[k] for k in sorted(set(base) & set(variant))]


def summarize_ratios(ratios, seed=20260913):
    """Median, geometric mean with a 95% bootstrap interval, wins, pairs."""
    if not ratios:
        return None
    rng = random.Random(seed)
    logs = [math.log(x) for x in ratios]
    boot = sorted(math.exp(statistics.mean(rng.choices(logs, k=len(logs))))
                  for _ in range(4000))
    return dict(median=statistics.median(ratios), geomean=geometric(ratios),
                ci_low=boot[99], ci_high=boot[3899],
                won=sum(x > 1 for x in ratios), pairs=len(ratios))


def magnitude(ratio):
    """Distance from 1 regardless of sign: 1.10x faster and slower both 1.10."""
    return max(ratio, 1 / ratio)


def ratio_text(ratio):
    return f'{ratio:.2f}x' if ratio >= 1 else f'{1 / ratio:.2f}x slower'


class Floors:
    """Resolution floors: |control vs baseline| per graph, and per allocation.

    The allocation floor is the worst graph's, which is the number a reader
    should hold a cell from that allocation against: a graph's own floor comes
    from as few as eight pairs and can be small by luck.
    """

    def __init__(self):
        self.by_graph = {}
        self.by_allocation = defaultdict(lambda: 1.0)

    def add(self, allocation, graph, summary):
        if summary is None:
            return
        value = magnitude(summary['median'])
        self.by_graph[(allocation, graph)] = value
        self.by_allocation[allocation] = max(self.by_allocation[allocation], value)

    def graph(self, allocation, graph):
        return self.by_graph.get((allocation, graph))

    def allocation(self, allocation):
        return self.by_allocation.get(allocation)

    def verdict(self, allocation, graph, summary, clean):
        """'clears' the allocation floor, 'own floor only', 'within', or why not.

        A cell with any failure in either arm is not resolved whatever its ratio
        says, because its ratio is over the runs that happened to finish.
        """
        if summary is None:
            return 'no pairs'
        if not clean:
            return 'survivors only'
        own, alloc = self.graph(allocation, graph), self.allocation(allocation)
        if own is None:
            return 'no floor'
        value = magnitude(summary['median'])
        if value > alloc:
            return 'clears'
        if value > own:
            return 'own floor only'
        return 'within floor'
