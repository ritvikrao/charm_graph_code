#!/bin/bash
#
# Step 7's A/B runs on exclusive Delta CPU nodes. One node by default; pass
# -N 2 to sbatch for the two-node run, which is where a mechanism that changes
# message traffic has to prove itself, since on one node every send is a
# pointer handoff.
#
#   sbatch [-N 2] scripts/ab_delta.sbatch <variants-file> [scale] [outdir]
#
# Submit each job from its own copy of the tree. Two jobs building in one
# checkout delete each other's objects -- see scripts/diagnose.sh.
#
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=64
#SBATCH --mem=0
#SBATCH --partition=cpu
#SBATCH --account=mzu-delta-cpu
#SBATCH --exclusive
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-ab
#SBATCH --output=%x-%j.out

set -uo pipefail
cd "$SLURM_SUBMIT_DIR" || exit 1

VARIANTS="${1:?variants file}"
SCALE="${2:-20}"
OUT="${3:-ab-out-$(basename "$VARIANTS" .txt)-${SLURM_NNODES}n}"

export SSSP_SCALE="$SCALE"
export SSSP_PPN="${SSSP_PPN:-16}"
export SSSP_REPS="${SSSP_REPS:-5}"
export SSSP_TIMEOUT="${SSSP_TIMEOUT:-900}"
export SSSP_CHARM_FLAGS="${SSSP_CHARM_FLAGS:-+setcpuaffinity}"
# A single node needs no launcher -- the PEs are threads in one process -- and
# adding one only introduces a variable. Two nodes need one process per node.
if [ "${SLURM_NNODES:-1}" -gt 1 ]; then
  export SSSP_LAUNCH="srun"
fi

echo "host=$(hostname) nodes=${SLURM_NNODES:-1} variants=$VARIANTS scale=$SCALE ppn=$SSSP_PPN reps=$SSSP_REPS"
scripts/diagnose.sh ab "$OUT" "$VARIANTS"
echo "== report =="
scripts/diag_report.py "$OUT" ab
