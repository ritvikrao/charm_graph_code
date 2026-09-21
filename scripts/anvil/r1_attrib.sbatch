#!/usr/bin/env bash
# R1 distributed-work attribution on Anvil: batch 8 at four layouts interleaved
# in one two-node allocation (see design/ipdps27-r1-attribution.md). Submit
# twice. Reservation cap per allocation: 2 x 128 cores x 20 min = 85.3 SU.
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --time=00:20:00
#SBATCH --job-name=acic-R1-attrib
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
APP=${ACIC_BENCH_APP:?frozen harness copy}
source /anvil/scratch/x-rrao/acic/env.sh
export ACIC_SRUN_MPI=pmi2
test "$SLURM_NNODES" -eq 2
for graph in mesh26-z road-usa-z; do
  python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$VARIANTS" \
    --sources 2 --reps 3 --source-role tune --batch
  python3 "$APP/benchmarks/work_cost_report.py" "$ROOT/logs/AB-$graph-${SLURM_NNODES}n-$SLURM_JOB_ID"
done
python3 "$APP/benchmarks/check_priority_runs.py" "$ROOT" "$SLURM_JOB_ID" \
  --config "$VARIANTS" --output "$ROOT/logs/R1-attrib-audit-$SLURM_JOB_ID.json"
