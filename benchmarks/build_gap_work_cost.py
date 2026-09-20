#!/usr/bin/env python3
"""Add counters to a private copy of pinned GAPBS, preserving its algorithm.

Every edit must match exactly once. The upstream checkout is never changed;
the generated source and its provenance remain beside the diagnostic binary.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('gapbs', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    app = Path(__file__).resolve().parents[1]
    args.output.mkdir(parents=True, exist_ok=False)
    source = args.gapbs / 'src/sssp.cc'
    text = source.read_text()

    def insert(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise ValueError(f'upstream changed: expected one occurrence of {old!r}')
        text = text.replace(old, new)

    insert('\n  for (WNode wn : g.out_neigh(u)) {',
           '\n  COST_ADD(EXPANSIONS, 1);\n  COST_ADD(EDGE_ATTEMPTS, g.out_degree(u));\n'
           '  for (WNode wn : g.out_neigh(u)) {')
    insert('    while (new_dist < old_dist) {',
           '    while (new_dist < old_dist) {\n      COST_ADD(CAS_ATTEMPTS, 1);')
    insert('        local_bins[dest_bin].push_back(wn.v);',
           '        COST_ADD(CHANGES, 1);\n        COST_ADD(QUEUE_PUSHES, 1);\n'
           '        local_bins[dest_bin].push_back(wn.v);')
    insert('      old_dist = dist[wn.v];',
           '      COST_ADD(CAS_FAILURES, 1);\n      old_dist = dist[wn.v];')
    insert('    vector<vector<NodeID> > local_bins(0);',
           '    work_cost::start(omp_get_thread_num());\n'
           '    vector<vector<NodeID> > local_bins(0);')
    insert('        NodeID u = frontier[i];',
           '        COST_ADD(QUEUE_POPS, 1);\n        NodeID u = frontier[i];')
    insert('        if (dist[u] >= delta * static_cast<WeightT>(curr_bin_index))\n'
           '          RelaxEdges(g, u, delta, dist, local_bins);\n      }',
           '        if (dist[u] >= delta * static_cast<WeightT>(curr_bin_index))\n'
           '          RelaxEdges(g, u, delta, dist, local_bins);\n'
           '        else COST_ADD(STALE_POPS, 1);\n      }')
    insert('        for (NodeID u : curr_bin_copy)\n          RelaxEdges(g, u, delta, dist, local_bins);',
           '        for (NodeID u : curr_bin_copy) {\n'
           '          COST_ADD(QUEUE_POPS, 1);\n'
           '          RelaxEdges(g, u, delta, dist, local_bins);\n        }')
    insert('    #pragma omp single\n    if (logging_enabled)',
           '    work_cost::stop();\n'
           '    std::copy(work_cost::counts, work_cost::counts + work_cost::COUNT,\n'
           '              gap_worker_cost[omp_get_thread_num()].begin());\n'
           '    #pragma omp single\n    if (logging_enabled)')
    generated = args.output / 'sssp_work_cost.inc'
    generated.write_text(text)
    driver = args.output / 'gap_driver.cpp'
    header = args.output / 'work_cost.h'
    driver.write_bytes((app / 'benchmarks/gap_driver.cpp').read_bytes())
    header.write_bytes((app / 'work_cost.h').read_bytes())
    binary = args.output / 'gap_work_cost'
    command = ['g++', '-O3', '-g', '-std=c++17', '-fopenmp', '-DACIC_WORK_COST',
               '-I' + str(args.gapbs / 'src'), '-I' + str(app),
               '-I' + str(args.output), '-I' + str(app / 'benchmarks'),
               str(driver), '-o', str(binary)]
    subprocess.run(command, check=True)
    paths = [source, generated, header, driver, binary]
    (args.output / 'manifest.json').write_text(json.dumps(dict(command=command,
        gap_revision=subprocess.check_output(['git', '-C', str(args.gapbs), 'rev-parse', 'HEAD'], text=True).strip(),
        sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}), indent=2) + '\n')
    print(binary)


if __name__ == '__main__':
    main()
