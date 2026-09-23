#!/usr/bin/env bash
# Frozen-binary RMAT regression gate (design/sc27-plan.md P2): the accepted C6
# candidate against frozen R0 and a repeat of R0, four held-out sources, on the
# five scale-free regression graphs. Run it in two allocations, then
#   python3 benchmarks/onenode_accept.py CAMPAIGN --variant n8_16x7_s8 \
#     --regression-reps 8 --allocation C6_A,GAP_A,ONE_A,THIS_A --allocation C6_B,GAP_B,ONE_B,THIS_B
# REPS (default 8) timed launches per arm: onenode_accept.py takes the median
# over them, and launch_modes.py reports fast-mode times and slow-launch counts
# in case Anvil launches are bimodal as Frontier's are.
#
#   sbatch -N 8 --time=01:30:00 scripts/anvil/rmat_gate.sbatch \
#     /anvil/scratch/$USER/acic/campaign $APP/benchmarks/rmat-gate-8n-variants.json
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --job-name=acic-rmat-gate
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
APP=${ACIC_BENCH_APP:?frozen harness copy}
source /anvil/scratch/x-rrao/acic/env.sh
export ACIC_SRUN_MPI=pmi2
GRAPHS=${GRAPHS:-rmat25,orkut,uniform25,rmat26,rmat27}
REPS=${REPS:-8}
sha256sum "$ROOT"/bin/acic_slice "$ROOT"/bin/acic_r0_control
for graph in ${GRAPHS//,/ }; do
  python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$VARIANTS" \
    --sources 4 --reps "$REPS" --source-role test --batch
done
python3 "$APP/benchmarks/check_priority_runs.py" "$ROOT" "$SLURM_JOB_ID" \
  --config "$VARIANTS" --graphs "$GRAPHS" --sources 4 --source-role test --reps "$REPS" \
  --output "$ROOT/logs/rmat-gate-audit-$SLURM_JOB_ID.json"
python3 "$APP/benchmarks/launch_modes.py" $(for graph in ${GRAPHS//,/ }; do echo "$ROOT/logs/AB-$graph-${SLURM_NNODES}n-$SLURM_JOB_ID"; done) \
  --output "$ROOT/logs/launch-modes-$SLURM_JOB_ID.json"
echo "RMAT GATE COMPLETE $SLURM_JOB_ID"
