#!/usr/bin/env bash
# Tune GAPBS using the existing external harness, then count buckets at the
# selected settings. Training sources choose settings; tests stay held out.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-gap-GAPBS
set -euo pipefail
ROOT=${1:?campaign directory}
GRAPHS=${2:-road-usa-z,mesh24-z,mesh26-z}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export SLURM_MPI_TYPE=cray_shasta PYTHONNOUSERSITE=1 OMP_PROC_BIND=close OMP_PLACES=cores
if [ -z "${GAP_SELECTION_JOB:-}" ]; then
python3 "$APP/benchmarks/run.py" "$ROOT" --mode external --external-arms gap \
  --graphs "$GRAPHS" --workers 120 --gap-threads 8,16,32,64,96,120,127 \
  --delta-divisors 256,64,16,4,1 --sources 4 --reps 3 --timeout 300
fi
python3 - "$ROOT" "${GAP_SELECTION_JOB:-$SLURM_JOB_ID}" "$APP" <<'PY'
import json, os, subprocess, sys
from pathlib import Path
root, job, app = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
sys.path.insert(0, str(app / 'benchmarks'))
from check_onenode_digest import check
records = [json.loads(line) for line in (root / 'logs' / f'external-1n-120w-{job}.jsonl').read_text().splitlines()]
out = root / 'logs' / f'GAP-D0-{job}'
out.mkdir(exist_ok=True)
seen = set()
for r in records:
    if r.get('config', {}).get('engine') != 'gap' or r.get('phase') != 'external' or not r.get('valid'): continue
    key = (r['graph'], r['source'])
    if key in seen: continue
    seen.add(key)
    config = r['config']
    log = out / f'{key[0]}-{key[1]}.out'
    with log.open('w') as stream:
        subprocess.run([str(root/'bin/gap_sssp'), str(root/'graphs'/f'{key[0]}.wsg'),
                        str(key[1]), str(config['delta']), '--diag'], check=True,
                       stdout=stream, stderr=subprocess.STDOUT,
                       env=dict(os.environ, OMP_NUM_THREADS=str(config['threads'])))
    check(root/'graphs'/f'{key[0]}.reference.txt', key[1], log)
if not seen: raise SystemExit('No held-out GAPBS records; inspect harness schema')
PY
