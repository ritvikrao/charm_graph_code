#!/usr/bin/env bash
# Fixed-layout scaling check (design/ipdps27-layout-check.md): batch 8 at
# 8x15, 16x7 and 8x7 per node, one job per node count. Submit with --nodes.
# GRAPHS (comma-separated) restricts the graphs; default mesh26-z,road-usa-z.
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --time=00:12:00
#SBATCH --job-name=acic-layout
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
APP=${ACIC_BENCH_APP:?frozen harness copy}
source /anvil/scratch/x-rrao/acic/env.sh
export ACIC_SRUN_MPI=pmi2
GRAPHS=${GRAPHS:-mesh26-z,road-usa-z}
for graph in ${GRAPHS//,/ }; do
  python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$VARIANTS" \
    --sources 2 --reps 3 --source-role tune --batch
done
python3 "$APP/benchmarks/check_priority_runs.py" "$ROOT" "$SLURM_JOB_ID" \
  --config "$VARIANTS" --graphs "$GRAPHS" --output "$ROOT/logs/layout-audit-$SLURM_JOB_ID.json"
