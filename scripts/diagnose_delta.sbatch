#!/bin/bash
#
# Step 6's measurements on a Delta compute node. A login node is fine for
# shaking the harness out and useless for anything timed: the H1 and H4 sweeps
# compare runs against each other, and on a shared node the difference between
# two configurations is smaller than the difference between two neighbours.
#
#   sbatch scripts/diagnose_delta.sbatch <h1|h2|h3|h4|all> [scale] [outdir]
#
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=64
#SBATCH --mem=0
#SBATCH --partition=cpu
#SBATCH --account=mzu-delta-cpu
#SBATCH --exclusive
#SBATCH --time=04:00:00
#SBATCH --job-name=acic-diag
#SBATCH --output=%x-%j.out

set -uo pipefail
cd "$SLURM_SUBMIT_DIR" || exit 1

EXPERIMENT="${1:-all}"
SCALE="${2:-20}"
OUT="${3:-diag-out-s$SCALE}"

# One process, threads inside it: an SMP build's PEs are threads, so a single
# node needs no launcher at all and adding one only introduces a variable.
# Affinity is pinned because H1 and H4 are timed comparisons, and an unpinned
# worker thread migrating between NUMA domains moves a measurement by more than
# the effects being measured.
export SSSP_SCALE="$SCALE"
export SSSP_PPN="${SSSP_PPN:-16}"
export SSSP_REPS="${SSSP_REPS:-5}"
export SSSP_TIMEOUT="${SSSP_TIMEOUT:-900}"
export SSSP_CHARM_FLAGS="${SSSP_CHARM_FLAGS:-+setcpuaffinity}"

echo "host=$(hostname) experiment=$EXPERIMENT scale=$SCALE ppn=$SSSP_PPN reps=$SSSP_REPS"
scripts/diagnose.sh "$EXPERIMENT" "$OUT"
echo "== report =="
scripts/diag_report.py "$OUT"
