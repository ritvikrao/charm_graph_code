#!/usr/bin/env bash
# Pilot (design/ipdps27-pilot.md): ACIC arms with +old-scheduler on mesh and
# road, then, if GAP=1, GAPBS tuning and timing in the same allocation.
# Submit with --nodes and --time.
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --job-name=acic-pilot
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
APP=${ACIC_BENCH_APP:?frozen harness copy}
source /anvil/scratch/x-rrao/acic/env.sh
export ACIC_SRUN_MPI=pmi2
for graph in mesh26-z road-usa-z; do
  python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$VARIANTS" \
    --sources 2 --reps 3 --source-role tune --batch
done
python3 "$APP/benchmarks/check_priority_runs.py" "$ROOT" "$SLURM_JOB_ID" \
  --config "$VARIANTS" --output "$ROOT/logs/pilot-audit-$SLURM_JOB_ID.json"
if [ "${GAP:-0}" = 1 ]; then
  python3 "$APP/benchmarks/gap_anvil.py" "$ROOT"
fi
