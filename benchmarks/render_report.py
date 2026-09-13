#!/usr/bin/env python3
"""Refresh generated tables in the step 7.5 decision report."""
import csv
import gzip
import json
from pathlib import Path
import re
import sys

data, report = map(Path, sys.argv[1:3])
document = report.read_text()


def section(name, value):
    global document
    start, end = f'<!-- BEGIN {name} -->', f'<!-- END {name} -->'
    block = start+'\n'+value.strip()+'\n'+end
    pattern = re.escape(start)+'.*?'+re.escape(end)
    if re.search(pattern, document, flags=re.S):
        document = re.sub(pattern, lambda _: block, document, flags=re.S)
    else:
        marker = f'<!-- {name} -->'
        if marker not in document:
            raise ValueError('Missing report section '+name)
        document = document.replace(marker, block)


graphs = json.loads((data/'graphs.json').read_text())
table = ['| Input | Vertices | Directed arcs stored | Maximum weight |',
         '|---|---:|---:|---:|']
for graph, details in sorted(graphs.items()):
    meta = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', details['metadata'])}
    table.append(f'| {graph} | {meta["vertices"]:,} | {meta["arcs"]:,} | {meta["max_weight"]:,} |')
section('INPUT_TABLE', '\n'.join(table))
for section_name, filename in {
    'PRIMARY_TABLE':'table.md', 'GLUON_TABLE':'gluon.md', 'NUMA_TABLE':'numa.md',
    'LAYOUT_TABLE':'layout-confirmation.md', 'CONFIRMATION_TABLE':'confirmation.md',
    'PRESOLVE_TABLE':'presolve.md', 'REPLAY_TABLE':'failure-replay.md',
    'QUIET_TABLE':'quiet.md',
}.items():
    section(section_name, (data/filename).read_text())
table = ['| Nodes × 16 workers | Current: delivered updates (M) | Fixed: delivered updates (M) | Current reductions | Fixed reductions | Current coarsenings |',
         '|---:|---:|---:|---:|---:|---:|']
counters = list(csv.DictReader((data/'counters.csv').open()))
for nodes in [1, 2, 4, 8, 16]:
    rows = {r['variant']: r for r in counters if r['nodes']==str(nodes) and r['workers']=='16' and r['graph']=='mesh22'}
    current, fixed = rows['current'], rows['tuned-fixed']
    table.append(f'| {nodes} | {float(current["updates_noted_median"])/1e6:.2f} | '
                 f'{float(fixed["updates_noted_median"])/1e6:.2f} | '
                 f'{float(current["reductions_median"]):,.1f} | {float(fixed["reductions_median"]):,.1f} | '
                 f'{float(current["coarsenings_median"]):.0f} |')
section('COUNTER_TABLE', '\n'.join(table))
main = [json.loads(s) for s in gzip.open(data/'runs.jsonl.gz', 'rt')]
extra = [json.loads(s) for s in gzip.open(data/'extra-runs.jsonl.gz', 'rt')]
primary = [r for r in main if r['phase']=='test']
# completion-test appears in both archives intentionally; count it only once.
supplemental = [r for r in extra if not r.get('skipped') and r['phase'] in ['gluon-test','confirm-test','numa-test','layout-test','quiet-test']]
section('PROVENANCE_SUMMARY', f'''The primary matrix contains **{len(list(csv.DictReader((data/'summary.csv').open())))} graph/resource cells,
{len(primary):,} attempted test queries ({sum(r['valid'] for r in primary):,} valid)**,
plus {len(supplemental):,} supplemental test attempts
({sum(r['valid'] for r in supplemental):,} valid; {sum(bool(r.get('skipped')) for r in extra)}
additional planned slots explicitly skipped after validation failure). Tuning, warmups,
presolve probes, failed adapters, and diagnostic replays are additional records.
See [provenance.md](step75-data/provenance.md) for pinned revisions and binary
identities, [job-accounting.psv](step75-data/job-accounting.psv) for Slurm states,
and [scratch-log-manifest.json](step75-data/scratch-log-manifest.json) for raw-log
locations and hashes. The initial build manifest predates the first commit;
the measured solver source is verified against commit `f29245e` by SHA-256.''')
report.write_text(document)
print(f'Refreshed {report}')
