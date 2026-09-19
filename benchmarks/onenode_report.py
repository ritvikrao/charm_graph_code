#!/usr/bin/env python3
"""D0 table from PREFIX.out and PREFIX.rounds.csv; no invented attribution.

Round latency, idle participation, and repeated work overlap: they are not
additive portions of wall time. The direct timer reports work/send/other;
the remaining columns diagnose why those PE-seconds were spent.
"""
import argparse
import csv
import math
import re
from pathlib import Path


def fields(text, tag):
    # Slurm merges concurrent rank output. A complete Main record can begin
    # immediately after a partial per-PE line, without a preceding newline.
    # Accept that prefix, but reject duplicate records or damage within one.
    matches = re.findall(re.escape(tag) + r' ([^\r\n]*)', text)
    if len(matches) != 1:
        raise ValueError(f'expected one {tag} record, found {len(matches)}')
    result = {}
    for token in matches[0].split():
        match = re.fullmatch(r'(\w+)=([0-9.eE+-]+)', token)
        if not match or match[1] in result:
            raise ValueError(f'malformed {tag} record: {token}')
        value = float(match[2])
        if not math.isfinite(value):
            raise ValueError(f'nonfinite {tag} field: {token}')
        result[match[1]] = value
    if not result:
        raise ValueError(f'empty {tag} record')
    return result


def summarize(prefix):
    log = Path(str(prefix) + '.out').read_text()
    comm = fields(log, 'COMM_SHARE')
    changes = fields(log, 'ONENODE_CHANGES')
    if changes['same_pe'] + changes['cross_pe'] != changes['total']:
        raise ValueError('distance-change provenance does not sum to total')
    with open(str(prefix) + '.rounds.csv') as stream:
        rounds = list(csv.DictReader(stream))
    duration = sum(float(r['round_seconds']) for r in rounds)
    idle = sum(float(r['round_seconds']) * (1 - int(r['active_pes']) / comm['pes'])
               for r in rounds) / duration
    work = comm['work_seconds'] - comm['work_send_seconds']
    return [prefix.name, f"{comm['solve_seconds']:.6f}", str(len(rounds)),
            f'{1e6 * duration / len(rounds):.1f}', f'{100 * idle:.1f}',
            f"{changes['total'] / changes['vertices']:.2f}",
            f"{changes['same_pe'] / changes['vertices']:.2f}",
            f"{changes['cross_pe'] / changes['vertices']:.2f}",
            f"{comm['compute_share']:.3f}", f"{comm['send_share']:.3f}",
            f"{comm['other_share']:.3f}", f"{1e9 * work / max(1, changes['total']):.1f}"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    print('| run | solve s | rounds | mean round us | inactive PE-time % | changes/V | same PE/V* | cross PE/V | work share | send share | other share | work ns/change |')
    print('|' + '---|' * 12)
    for path in sorted(args.directory.rglob('*.rounds.csv')):
        prefix = Path(str(path)[:-len('.rounds.csv')])
        print('| ' + ' | '.join(summarize(prefix)) + ' |')
    print('\n*Same-PE count includes the injected source. Inactivity is time-weighted '
          'round participation, not measured CPU idle time. Columns overlap; '
          'diagnostic timings must not be used for adoption A/Bs.')


if __name__ == '__main__':
    main()
