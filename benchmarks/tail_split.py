#!/usr/bin/env python3
"""Split each ACIC solve into bulk (to 95% of updates created) and tail.

The step-8/J5 bimodality lives entirely in the tail; this reports, per
launch, the bulk time, the tail time, tail rounds by length, and any CXI
provider log lines, so launch modes can be told apart without a new build.
"""
import argparse
import json
from pathlib import Path
import re
import statistics as st


def split(text, seconds):
    begin = float(re.search(r'Beginning at time: ([\d.]+)', text)[1])
    trace = [(int(c), float(t)) for c, t in
             re.findall(r'^Updates: created: (\d+),.*?t= ([\d.]+)$', text, flags=re.M)]
    total = trace[-1][0]
    i95 = next(i for i, (c, _) in enumerate(trace) if c >= 0.95 * total)
    bulk = trace[i95][1] - begin
    gaps = [b[1] - a[1] for a, b in zip(trace[i95:], trace[i95 + 1:])]
    return dict(seconds=seconds, bulk=bulk, tail=seconds - bulk, tail_rounds=len(gaps),
                long_rounds=sum(g >= 0.01 for g in gaps), long_seconds=sum(g for g in gaps if g >= 0.01))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    args = ap.parse_args()
    rows = [json.loads(l) for l in (args.directory/'runs.jsonl').read_text().splitlines()]
    launches = {}
    for r in rows:
        text = (args.directory/f"{r['variant']}-s{r['source_index']}-r{r['rep']}.out").read_text()
        launches.setdefault((r['variant'], r['rep']), []).append(split(text, r['seconds']))
    output = []
    for (variant, rep), solves in sorted(launches.items()):
        log = (args.directory/f'{variant}-g0-r{rep}.launch.out').read_text()
        # CXI warnings, from the launch log or, under ACIC_STDERR_DIR, from the
        # per-rank stderr files named by the launch's PROBE_STEP line.
        step = re.search(r'^PROBE_STEP (\S+)$', log, flags=re.M)
        if step:
            log = ''.join(p.read_text(errors='replace') for p in
                          args.directory.parent.glob(f'cxi-stderr/*/{step[1]}.rank*.err'))
        # Huge pages actually backing each rank (ACIC_MEMLOG_DIR): the
        # largest sample per rank, summed over ranks, in GB.
        huge = rss = 0.0
        remote = []
        if step:
            for p in args.directory.parent.glob(f'memlog/*/{step[1]}.rank*.mem'):
                samples = [dict(zip(l.split()[::2], map(int, l.split()[1::2])))
                           for l in p.read_text().splitlines() if l.strip()]
                huge += max((x.get('AnonHugePages:', 0) for x in samples), default=0) / 2**20
                rss += max((x.get('Rss:', 0) for x in samples), default=0) / 2**20
                # The rank's NUMA spread at its largest sample: the fraction of
                # its pages outside the node that holds most of them.
                numa = [{k: v for k, v in x.items() if re.fullmatch(r'N\d+:', k)} for x in samples]
                numa = max(numa, key=lambda x: sum(x.values()), default={})
                if sum(numa.values()):
                    remote.append(1 - max(numa.values()) / sum(numa.values()))
        cxi = re.findall(r'libfabric:\d+:\d+::cxi:[^:]*:([a-z_]+)\(\)', log)
        output.append(dict(variant=variant, rep=rep,
            **{k: st.mean(s[k] for s in solves) for k in solves[0]},
            huge_gb=huge, rss_gb=rss, worst_remote=max(remote, default=0.0), remote_ranks=sum(r > 0.1 for r in remote), cxi_lines=len(cxi), cxi_kinds=dict(__import__('collections').Counter(cxi).most_common(4))))
    for o in output:
        print('%-14s r%2d  secs %.3f  bulk %.3f  tail %.3f  rounds %4.0f  >=10ms %4.1f (%.3f s)  huge %.1f/%.1f GB  remote %.2f (%d ranks)  cxi %d %s' % (
            o['variant'], o['rep'], o['seconds'], o['bulk'], o['tail'], o['tail_rounds'],
            o['long_rounds'], o['long_seconds'], o['huge_gb'], o['rss_gb'], o['worst_remote'], o['remote_ranks'], o['cxi_lines'], o['cxi_kinds']))
    (args.directory/'tail-split.json').write_text(json.dumps(output, indent=2) + '\n')


if __name__ == '__main__':
    main()
