#!/usr/bin/env bash
# Frontier equivalent of scripts/anvil/pilot.sbatch: interleaved ACIC arms on
# mesh26-z and road-usa-z, two training sources, one warmup and three timed
# repetitions, then the strict raw-log audit.
#
# Frontier differences: Cray PMI, never --mpi=pmi2 (it gives every rank world
# size 1, so each process solves the whole graph alone and still prints the
# right answer); layouts come from benchmarks/machine.py, so a node's 56
# usable cores replace Delta/Anvil's 128.
#
#   sbatch scripts/frontier/onenode_ab.sbatch CAMPAIGN VARIANTS.json [graphs]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-ab
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
GRAPHS=${3:-mesh26-z,road-usa-z}
# SOURCES and ROLE select the sources (default: the two training sources).
SOURCES=${SOURCES:-2}
ROLE=${ROLE:-tune}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
for graph in ${GRAPHS//,/ }; do
  "$PY" "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$VARIANTS" \
    --sources "$SOURCES" --reps 3 --source-role "$ROLE" --batch
  # Work-cost arms ("work_cost": true) need work-cost.json for the audit.
  if grep -q '"work_cost": true' "$VARIANTS"; then
    "$PY" "$APP/benchmarks/work_cost_report.py" "$ROOT/logs/AB-$graph-${SLURM_NNODES}n-$SLURM_JOB_ID"
  fi
done
"$PY" "$APP/benchmarks/check_priority_runs.py" "$ROOT" "$SLURM_JOB_ID" \
  --config "$VARIANTS" --graphs "$GRAPHS" --sources "$SOURCES" --source-role "$ROLE" \
  --output "$ROOT/logs/onenode-audit-$SLURM_JOB_ID.json"
echo "AB COMPLETE $SLURM_JOB_ID"
