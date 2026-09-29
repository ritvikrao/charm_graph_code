#!/usr/bin/env python3
"""Machine-readable record of onenode_ab.py runs (the Delta IPDPS D items).

    ipdps_ab_summary.py OUT.json BASE AB_DIR [AB_DIR ...]

For each AB directory: solve counts and digest validity, per-arm per-source
medians of the timed repetitions, speedup of every arm over BASE (BASE time /
arm time, above 1 is faster), rounds, and the WORK_EFFICIENCY ranges; for an
arm X with an X_nocount twin, the counted/uncounted ratio (D2 overhead).
"""
import hashlib
import json
import statistics as st
import sys
from pathlib import Path


def summarize(directory, base):
    manifest = json.loads((directory / 'manifest.json').read_text())
    rows = [json.loads(line) for line in (directory / 'runs.jsonl').read_text().splitlines()]
    timed = [r for r in rows if r['rep'] >= 0]
    arms = [v['label'] for v in manifest['variants']]
    sources = sorted({r['source_index'] for r in timed})
    def median(arm, s):
        return st.median(r['seconds'] for r in timed if r['variant'] == arm and r['source_index'] == s)
    record = dict(directory=str(directory), graph=rows[0]['graph'], hosts=manifest.get('hosts'),
                  binaries={v['label']: dict(binary=v['binary'], sha256=v['sha256']) for v in manifest['variants']},
                  solves=len(rows), timed=len(timed), all_digests_valid=all(r['valid'] for r in rows),
                  reps=manifest['reps'], base=base, arms={})
    for arm in arms:
        xs = [r for r in timed if r['variant'] == arm]
        if not xs:
            continue
        a = dict(graph=xs[0]['graph'],
                 median_seconds_by_source=[median(arm, s) for s in sources],
                 median_rounds=st.median(r['rounds'] for r in xs) if all('rounds' in r for r in xs) else None)
        if base in arms:
            a['speedup_over_base_by_source'] = [median(base, s) / median(arm, s) for s in sources]
        eff = [r['efficiency'] for r in xs if 'efficiency' in r]
        for key in ['attempts_per_edge', 'wire_bytes_per_edge', 'node_bytes_per_edge']:
            if eff:
                a[key + '_range'] = [min(e[key] for e in eff), max(e[key] for e in eff)]
        record['arms'][arm] = a
    overhead = {}
    for arm in arms:
        if arm + '_nocount' in arms:
            ratios = [median(arm, s) / median(arm + '_nocount', s) for s in sources]
            overhead[arm] = dict(counted_over_nocount_by_source=ratios, geomean=st.geometric_mean(ratios))
    if overhead:
        record['counter_overhead'] = overhead
    return record


def main():
    out, base, *dirs = sys.argv[1:]
    records = [summarize(Path(d), base) for d in dirs]
    Path(out).write_text(json.dumps(records, indent=2) + '\n')
    for r in records:
        print(r['graph'], r['solves'], 'valid' if r['all_digests_valid'] else 'INVALID')
        for arm, a in r['arms'].items():
            sp = a.get('speedup_over_base_by_source')
            print(f"  {arm:16s} {min(sp):.3f}-{max(sp):.3f}x" if sp else f"  {arm}", a.get('median_rounds'),
                  a.get('attempts_per_edge_range'))


if __name__ == '__main__':
    main()
