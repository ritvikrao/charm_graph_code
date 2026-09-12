#!/bin/bash
#
# H4 on two nodes. The other three hypotheses are about the graph and the
# partition and are answered on one node; H4 is about the cost of a round, and
# a round is a reduction and a broadcast. On one node that is tens of
# microseconds and on sixteen it is not, so a tail conclusion drawn from a
# single node would be answering an easier question than the one the 2024
# results pose.
#
#   sbatch scripts/diagnose_delta_2node.sbatch [scale] [outdir]
#
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=64
#SBATCH --mem=0
#SBATCH --partition=cpu
#SBATCH --account=mzu-delta-cpu
#SBATCH --exclusive
#SBATCH --time=04:00:00
#SBATCH --job-name=acic-diag-2n
#SBATCH --output=%x-%j.out

set -uo pipefail
cd "$SLURM_SUBMIT_DIR" || exit 1

SCALE="${1:-20}"
OUT="${2:-diag-out-2node-s$SCALE}"

export SSSP_SCALE="$SCALE"
export SSSP_PPN="${SSSP_PPN:-16}"
export SSSP_REPS="${SSSP_REPS:-5}"
export SSSP_TIMEOUT="${SSSP_TIMEOUT:-900}"
export SSSP_LAUNCH="srun"
export SSSP_CHARM_FLAGS="${SSSP_CHARM_FLAGS:-+setcpuaffinity}"

echo "nodes=$SLURM_NNODES scale=$SCALE ppn=$SSSP_PPN reps=$SSSP_REPS"
scripts/diagnose.sh h4 "$OUT"
echo "== report =="
scripts/diag_report.py "$OUT" h4
