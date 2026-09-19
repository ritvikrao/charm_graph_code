#!/usr/bin/env python3
"""Fail closed on missing/wrong full digests, truncated runs, and rescues."""
import re
import sys
from pathlib import Path


def check(reference, source, log):
    rows = [r.split() for r in Path(reference).read_text().splitlines() if r[:1].isdigit()]
    ref = next(r for r in rows if r[0] == str(source))
    text = Path(log).read_text()
    if re.search(r'PROGRESS_STALL|CONSERVATION VIOLATED|STALL_RESCUE|TRUNCATED|VERIFY FAIL', text):
        raise ValueError(f'{log}: stalled, truncated, or failed')
    matches = re.findall(r'(?:VERIFY parallel digest|BENCH source=\d+ solve_seconds=[\d.eE+-]+) h1=(\d+) h2=(\d+) reachable=(\d+) distance_sum=(\d+)', text)
    if len(matches) != 1:
        raise ValueError(f'{log}: expected exactly one digest, found {len(matches)}')
    got = list(matches[0])
    if got != ref[2:6]:
        raise ValueError(f'{log}: digest {got} != {ref[2:6]}')


if __name__ == '__main__':
    check(*sys.argv[1:])
