#!/usr/bin/env bash
# D7 stage 2: ACIC's arms (benchmarks/onenode_ab.py --batch, 16 x 7,
# digest-checked, rotated), then GAPBS and Wasp at the selections frozen by
# d7_tune.sbatch job TUNE_JOB (onenode_gap_tune.py --selection-job: one warmup
# per held-out source, then REPS repetitions in random order), all on the same
# four held-out sources in one allocation.
#
#   sbatch scripts/delta/d7_compare.sbatch CAMPAIGN VARIANTS.json GRAPH TUNE_JOB
# REPS (default 3) applies to every engine; ENGINES (default gap,wasp).
#SBATCH --account=rfp-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=16
#SBATCH --cpus-per-task=8
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-d7-compare
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
GRAPH=${3:?graph}
TUNE_JOB=${4:?tuning job}
REPS=${REPS:-3}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export ACIC_AB_STEP_MINUTES=${STEP_MINUTES:-8} ACIC_AB_TIMEOUT=${SOLVE_TIMEOUT:-300}
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_PROF_TIMER ACIC_PROF_OFF ACIC_PROF_DIR
echo "job=$SLURM_JOB_ID host=$(hostname) graph=$GRAPH variants=$VARIANTS tune_job=$TUNE_JOB reps=$REPS"
sha256sum "$VARIANTS" "$ROOT/bin/gap_sssp" "$ROOT/bin/wasp_sssp"
python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$GRAPH" "$VARIANTS" \
  --rpn 16 --workers 112 --sources 4 --reps "$REPS" --source-role test --batch
ENG=${ENGINES:-gap,wasp}
for e in ${ENG//,/ }; do
  echo "== $e $(date -Is)"
  python3 "$APP/benchmarks/onenode_gap_tune.py" "$ROOT" --graphs "$GRAPH" --engine "$e" \
    --selection-job "$TUNE_JOB" --sources 4 --reps "$REPS" --launch-timeout "${LAUNCH_TIMEOUT:-240}"
done
echo "D7 COMPARE COMPLETE $SLURM_JOB_ID"
