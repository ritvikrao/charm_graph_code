#!/usr/bin/env bash
# Batch the two sources per launch to avoid rereading the same CSR.
# 8-node cap: 1024 cores * 6/60 h = 102.4 SU per allocation.
# 1-node cap: 128 cores * 12/60 h = 25.6 SU; includes matched GAPBS work counts.
# Submit both roles twice; total allocation-limit exposure 256 SU.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=00:12:00
#SBATCH --job-name=acic-R0
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
for graph in mesh26-z road-usa-z; do
  python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$VARIANTS" \
    --sources 2 --reps 2 --source-role tune --batch
  python3 "$APP/benchmarks/work_cost_report.py" "$ROOT/logs/AB-$graph-${SLURM_NNODES}n-$SLURM_JOB_ID"
done
if [ "$SLURM_NNODES" = 1 ]; then
  python3 "$APP/benchmarks/work_cost_gap.py" "$ROOT"
fi
