#!/usr/bin/env bash
# Delta one-node IPDPS experiments D1-D3 (design/sc27-plan.md, "Delta, one
# node"): interleaved held-out comparisons through benchmarks/onenode_ab.py
# --batch. One launch reads the graph once and solves the four held-out
# sources; arms rotate every repetition; every solve is digest-checked against
# the independent reference and a wrong one stops the job. runs.jsonl is
# written line by line, so a job that reaches the one-hour interactive limit
# keeps every repetition it finished.
#
#   sbatch scripts/delta/ipdps_ab.sbatch CAMPAIGN VARIANTS.json GRAPH[,GRAPH...]
#
#   D1  benchmarks/delta-ipdps-d1-variants.json          grid3-30-z, the terrain crop
#   D1+D2 benchmarks/delta-ipdps-d2-mesh28-variants.json mesh28-z
#   D1+D2 benchmarks/delta-ipdps-d2-road-variants.json   road-usa-z
#   D3  benchmarks/delta-ipdps-d3-width-variants.json    mesh28-w10-z
#
# REPS (default 3) timed repetitions after one warmup; STEP_MINUTES (default
# 8) per launch, raised for the 51 GB grid3-30-z.
#SBATCH --account=rfp-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=16
#SBATCH --cpus-per-task=8
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-ipdps-ab
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
GRAPHS=${3:?graph[,graph...]}
REPS=${REPS:-3}
SOURCES=${SOURCES:-4}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export ACIC_AB_STEP_MINUTES=${STEP_MINUTES:-8} ACIC_AB_TIMEOUT=${SOLVE_TIMEOUT:-300}
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_PROF_TIMER ACIC_PROF_OFF ACIC_PROF_DIR
echo "job=$SLURM_JOB_ID host=$(hostname) variants=$VARIANTS graphs=$GRAPHS reps=$REPS"
sha256sum "$VARIANTS"; for b in "$ROOT"/bin/acic_*; do case $b in *.manifest) ;; *) sha256sum "$b" ;; esac; done
for graph in ${GRAPHS//,/ }; do
  python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$VARIANTS" \
    --rpn 16 --workers 112 --sources "$SOURCES" --reps "$REPS" --source-role test --batch
done
echo "IPDPS AB COMPLETE $SLURM_JOB_ID"
