#!/usr/bin/env python3
"""Compare Wasp's no-opt, pull-only, leaves-only and full SSSP on sparse graphs."""
import hashlib
import json
import os
from pathlib import Path
import random
import re
import statistics
import struct
import subprocess
import sys

import numpy as np

from check_onenode_digest import check


APP = Path(__file__).resolve().parents[1]
PROTOCOL = json.loads((APP / 'benchmarks/delta-wasp-feature-protocol.json').read_text())
GRAPH_ROOT = Path(PROTOCOL['graphs_root'])
FULL = Path(PROTOCOL['full_binary'])
GRAPHS = PROTOCOL['graphs']
MODES = tuple(PROTOCOL['modes'])
RNG = random.Random(20260925)


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def degree_profile(path):
    with path.open('rb') as f:
        header = f.read(17)
    assert header[0] == 0, 'Wasp feature driver uses undirected graphs'
    edges, vertices = struct.unpack_from('<qq', header, 1)
    offsets = np.memmap(path, dtype='<i8', mode='r', offset=17, shape=(vertices + 1,))
    counts = {str(degree): 0 for degree in range(5)}
    counts['5+'] = 0
    maximum = 0
    for start in range(0, vertices, 1 << 20):
        end = min(start + (1 << 20), vertices)
        degrees = np.diff(offsets[start:end + 1])
        for degree in range(5):
            counts[str(degree)] += int(np.count_nonzero(degrees == degree))
        counts['5+'] += int(np.count_nonzero(degrees >= 5))
        maximum = max(maximum, int(degrees.max()))
    assert sum(counts.values()) == vertices
    assert int(offsets[-1]) == edges
    return dict(vertices=vertices, edges=edges, average_degree=edges/vertices,
                degree_counts=counts, max_degree=maximum,
                leaf_fraction=counts['1']/vertices)


def main(root):
    assert int(os.environ['SLURM_NNODES']) == 1
    root = Path(root).resolve()
    out = root / ('features-' + os.environ['SLURM_JOB_ID'])
    out.mkdir(parents=True)
    feature_bin = root / 'bin/wasp_feature'
    wasp_source = Path.home() / 'acic-comparison-deps/wasp-ae/wasp-ae/impl/wasp/src/sssp-ablation.cc'
    manifest = dict(job=os.environ['SLURM_JOB_ID'], hosts=os.environ['SLURM_JOB_NODELIST'],
                    app_revision=subprocess.check_output(['git', '-C', str(APP), 'rev-parse', 'HEAD'], text=True).strip(),
                    full_binary_sha256=sha256(FULL), feature_binary_sha256=sha256(feature_bin),
                    wasp_ablation_source_sha256=sha256(wasp_source),
                    graphs={name: degree_profile(GRAPH_ROOT/(name+'.wsg')) for name in GRAPHS},
                    protocol=PROTOCOL)
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    rows = []
    for rep in range(-PROTOCOL['warmups_per_cell'],
                     PROTOCOL['timed_repetitions_per_cell']):
        cells = [(name, source, mode) for name, cfg in GRAPHS.items()
                 for source in cfg['sources'] for mode in MODES]
        RNG.shuffle(cells)
        for name, source, mode in cells:
            cfg = GRAPHS[name]
            log = out / f'{len(rows):03d}-{name}-{source}-{mode}-r{rep}.out'
            env = dict(os.environ, OMP_NUM_THREADS=str(cfg['threads']),
                       OMP_DYNAMIC='FALSE', OMP_PLACES='cores', OMP_PROC_BIND='close')
            cmd = ['srun', '-N1', '-n1', '-c128', '--cpu-bind=none', '--unbuffered',
                   '--kill-on-bad-exit=1', '--time=5',
                   str(FULL if mode == 'full' else feature_bin),
                   str(GRAPH_ROOT/(name+'.wsg')), source, str(cfg['delta'])]
            if mode != 'full':
                cmd.append(mode)
            with log.open('w') as stream:
                result = subprocess.run(cmd, stdout=stream, stderr=subprocess.STDOUT,
                                        env=env, timeout=360)
            assert result.returncode == 0, (cmd, log)
            check(GRAPH_ROOT/(name+'.reference.txt'), source, log)
            matches = re.findall(r'BENCH source=\d+ solve_seconds=([0-9.eE+-]+)',
                                 log.read_text())
            assert len(matches) == 1, log
            row = dict(graph=name, source=source, mode=mode, rep=rep,
                       phase='warmup' if rep < 0 else 'test',
                       seconds=float(matches[0]), digest_valid=True,
                       command=cmd, log=str(log))
            rows.append(row)
            with (out/'runs.jsonl').open('a') as stream:
                stream.write(json.dumps(row)+'\n')
            print(len(rows), row['phase'], name, source, mode, row['seconds'], 'PASS', flush=True)
    summary = dict(manifest=manifest, valid_solves=len(rows), results={})
    for name, cfg in GRAPHS.items():
        graph_rows = {}
        for source in cfg['sources']:
            medians = {mode: statistics.median(r['seconds'] for r in rows
                       if r['phase']=='test' and r['graph']==name
                       and r['source']==source and r['mode']==mode)
                       for mode in MODES}
            graph_rows[source] = dict(medians=medians,
                speedup_full_over_noopt=medians['noopt']/medians['full'],
                speedup_pull_over_noopt=medians['noopt']/medians['pull'],
                speedup_leaves_over_noopt=medians['noopt']/medians['leaves'])
        summary['results'][name] = graph_rows
    (out/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print('COMPLETE', out, summary['valid_solves'], flush=True)


if __name__ == '__main__':
    main(sys.argv[1])
