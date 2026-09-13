#!/usr/bin/env python3
"""Extract untimed ACIC counters from retained raw logs; never rerun a solve."""
from collections import defaultdict
import csv
import gzip
import json
from pathlib import Path
import re
import statistics
import sys

logs, output = map(Path, sys.argv[1:3])
groups = defaultdict(list)
for path in sorted(logs.glob('benchmark-*.log.gz')):
    current = None
    def finish():
        if current and current['phase'] == 'test' and current['valid'] and current['config']['engine'] == 'acic':
            key = (current['nodes'], current['workers'], current['graph'], current['config']['name'])
            groups[key].append(current)
    with gzip.open(path, 'rt') as f:
        for line in f:
            if line.startswith('RUN '):
                finish()
                current = json.loads(line[4:])
            elif current:
                match = re.match(r'Bucket scale: (\d+) \((\d+) coarsenings\)', line)
                if match:
                    current['bucket_scale'], current['coarsenings'] = map(int, match.groups())
    finish()

rows = []
for (nodes, workers, graph, name), records in sorted(groups.items()):
    row = dict(nodes=nodes, workers=workers, graph=graph, variant=name, runs=len(records))
    for field in ['seconds', 'reductions', 'updates_noted', 'distance_changes', 'rejected',
                  'tram_bytes', 'tram_messages', 'bucket_scale', 'coarsenings', 'read_seconds']:
        values = [r[field] for r in records if field in r]
        if len(values) == len(records):
            row[field+'_median'] = statistics.median(values)
    rows.append(row)
output.parent.mkdir(parents=True, exist_ok=True)
keys = list(dict.fromkeys(key for row in rows for key in row))
with output.open('w') as f:
    writer = csv.DictWriter(f, fieldnames=keys)
    writer.writeheader()
    writer.writerows(rows)
print(f'{len(rows)} ACIC counter groups written to {output}')
