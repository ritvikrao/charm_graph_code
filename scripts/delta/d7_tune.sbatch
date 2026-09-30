#!/usr/bin/env bash
# D7 (sc27-plan.md, Delta one node round 2), stage 1: tune GAPBS and Wasp on
# the same Delta node type ACIC runs on, threads x delta on the two training
# sources only (benchmarks/onenode_gap_tune.py --tune-only: a joint grid, then
# the top three repeated twice). Writes
# CAMPAIGN/logs/external-1n-120w-JOB-GRAPH-selected[-wasp].json; stage 2
# (d7_compare.sbatch) runs those frozen selections on the held-out sources in
# the same allocation as ACIC.
#
#   sbatch scripts/delta/d7_tune.sbatch CAMPAIGN GRAPH THREADS DELTAS
#   e.g. ... road-usa-z 32,64,96,128 2048,8192,32768,131072
# ENGINES (default gap,wasp) picks the engines; LAUNCH_TIMEOUT caps a launch.
#SBATCH --account=rfp-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=16
#SBATCH --cpus-per-task=8
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-d7-tune
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
GRAPH=${2:?graph}
THREADS=${3:?threads}
DELTAS=${4:?deltas}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
echo "job=$SLURM_JOB_ID host=$(hostname) graph=$GRAPH threads=$THREADS deltas=$DELTAS engines=${ENGINES:-gap,wasp}"
sha256sum "$ROOT/bin/gap_sssp" "$ROOT/bin/wasp_sssp"
ENG=${ENGINES:-gap,wasp}
for e in ${ENG//,/ }; do
  echo "== $e $(date -Is)"
  python3 "$APP/benchmarks/onenode_gap_tune.py" "$ROOT" --graphs "$GRAPH" --engine "$e" \
    --threads "$THREADS" --deltas "$DELTAS" --tune-only --spread --launch-timeout "${LAUNCH_TIMEOUT:-240}"
done
echo "D7 TUNE COMPLETE $SLURM_JOB_ID"
