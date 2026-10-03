#!/usr/bin/env python3
"""Export every per-solve record from the Frontier campaign to flat CSV files.

  export_frontier_csv.py [OUTDIR]        (default design/frontier-data)

Writes OUTDIR/frontier_solves.csv and OUTDIR/frontier_jobs.csv in the schema of
design/delta-data (export_delta_csv.py does the work; see OUTDIR/README.md).

Sources, all under the campaign's logs/: logs/AB-<graph>-<N>n-<job>/runs.jsonl
(onenode_ab.py: ACIC arms, harness `onenode_ab`) and logs/external-*.jsonl
(run.py --mode external: GAPBS, Wasp, Gluon, RIKEN, Gemini, HavoqGT, harness
`run.py`). A `*.failed` directory holds an A/B attempt whose audit failed;
gluon_compare.sbatch then reran the frozen arm alone in a new directory, so
those rows get outcome `failed-check`. Logs with no structured record (.out
only) are not read.
"""
import re, sys
from pathlib import Path

import export_delta_csv as ex

CAMPAIGN = Path('/lustre/orion/csc710/scratch/rrao/acic/campaign')
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ex.APP / 'design' / 'frontier-data'

# Jobs whose results are partial or replaced (current-state.md says why).
SUPERSEDED = {
    '5570584': 'partial: hit its 2-hour limit; mesh32-z not run, terrain30-s-z one repetition (sec. 38); F14c 5573561 followed',
    '5573561': 'partial: the 56 x 1 arm was killed at the 12-minute step limit; 8 x 7 only (sec. 40)',
}


def normalize(r, **kw):
    """export_delta_csv.normalize, plus the chunk band of the chunk-queue builds, which is compiled in
    (ACIC_PROCESS_CHUNKS) and named in the binary: acic_frz_c256, acic_frz_c65536, acic_c256."""
    row = _normalize(r, **kw)
    m = re.search(r'_c(\d+)(?:_|$)', row['binary'])
    if row['engine'] == 'acic' and m and not row['chunk_band']:
        row['chunk_band'] = m[1]
    row['launch_wall_seconds'] = r.get('launch_wall_seconds', '')
    return row


_normalize, ex.normalize = ex.normalize, normalize
# run.py records the wall time of each baseline launch. A launch stopped at its cap (outcome hang) has
# no solve time; results_by_dataset.py bounds its solve below by this time less 300 s of graph load.
ex.COLUMNS = ex.COLUMNS + ['launch_wall_seconds']

if __name__ == '__main__':
    ex.main(roots=[CAMPAIGN / 'logs'], outdir=OUT, prefix='frontier', campaign_of=lambda root: 'frontier-campaign',
            superseded=SUPERSEDED, failed_outcome='failed-check')
