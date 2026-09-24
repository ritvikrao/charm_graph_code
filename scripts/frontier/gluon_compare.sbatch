#!/usr/bin/env bash
# ACIC against Gluon (D-Galois sssp-push, Async and Sync) in one allocation.
#
# 1. ACIC: onenode_ab.py runs the arms of each graph's variants file on the
#    four held-out sources, one warmup and REPS repetitions. It runs first
#    because its length is known; Gluon's is not.
# 2. Gluon: run.py --mode external tunes each Gluon mode (layout on the first
#    training source, then delta on both) and runs the selections on the same
#    held-out sources, REPS repetitions. Selections go to
#    logs/external-<N>n-56w-<job>-<graph>-selected.json.
#
# Each graph is GRAPH:FAMILY, and FAMILY names
# benchmarks/$VARIANTS_PREFIX-FAMILY-<N>n-variants.json (VARIANTS_PREFIX
# defaults to frontier-gluon; the larger-input study uses frontier-large). Extra arguments go to
# run.py (e.g. --gluon-layout-divisor 1 --gluon-delta-multipliers 4,16,64 for
# meshes and roads, whose max distance is thousands of mid deltas).
#
#   sbatch -N 16 --time=02:00:00 scripts/frontier/gluon_compare.sbatch CAMPAIGN \
#       orkut:orkut,uniform25:scalefree,rmat25:scalefree
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=16
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-gluon
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
PAIRS=${2:?GRAPH:FAMILY,...}
shift 2
SOURCES=${SOURCES:-4}
REPS=${REPS:-3}
TIMEOUT=${TIMEOUT:-240}
VARIANTS_PREFIX=${VARIANTS_PREFIX:-frontier-gluon}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
N=$SLURM_NNODES
printf 'job=%s hosts=%s pairs=%s run.py args=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST" "$PAIRS" "$*"
GRAPHS=$(tr ',' '\n' <<< "$PAIRS" | cut -d: -f1 | paste -sd,)

# --- ACIC (LCI environment, as onenode_ab.sbatch).
(
  module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
  module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
  export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
  unset LCT_PMI_BACKEND ACIC_SRUN_MPI
  for pair in ${PAIRS//,/ }; do
    graph=${pair%%:*}
    variants=$APP/benchmarks/$VARIANTS_PREFIX-${pair#*:}-${N}n-variants.json
    # onenode_ab.py stops a graph at its first failed check (a stall is a
    # failure even when the answer is right), which also loses every other
    # arm. Keep that attempt aside and rerun the production arm alone.
    if ! "$PY" "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$variants" \
         --sources "$SOURCES" --reps "$REPS" --source-role test --batch; then
      echo "ACIC $graph: failed check; rerunning the frozen arm alone"
      mv "$ROOT/logs/AB-$graph-${N}n-$SLURM_JOB_ID" "$ROOT/logs/AB-$graph-${N}n-$SLURM_JOB_ID.failed"
      variants=$ROOT/logs/gluon-variants-$SLURM_JOB_ID-$graph-frozen.json
      "$PY" -c 'import json,sys; json.dump([v for v in json.load(open(sys.argv[1])) if v["label"] == "frozen"], open(sys.argv[2], "w"), indent=2)' \
        "$APP/benchmarks/$VARIANTS_PREFIX-${pair#*:}-${N}n-variants.json" "$variants"
      "$PY" "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$variants" \
        --sources "$SOURCES" --reps "$REPS" --source-role test --batch || echo "ACIC $graph frozen: exit $?"
    fi
    "$PY" "$APP/benchmarks/check_priority_runs.py" "$ROOT" "$SLURM_JOB_ID" \
      --config "$variants" --graphs "$graph" --sources "$SOURCES" --source-role test --reps "$REPS" \
      --output "$ROOT/logs/onenode-audit-$SLURM_JOB_ID-$graph.json" || echo "audit $graph: exit $?"
  done
)

# --- Gluon (Cray MPICH environment, as compare.sbatch).
[ -n "${SKIP_GLUON:-}" ] || (
  module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
  module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
  module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
  export MPICH_OFI_NIC_POLICY=ROUND-ROBIN
  unset LCT_PMI_BACKEND ACIC_SRUN_MPI
  sha256sum "$ROOT/bin/gluon_sssp"
  "$PY" -u "$APP/benchmarks/run.py" "$ROOT" --mode external --external-arms gluon-async,gluon-sync \
    --workers 56 --timeout "$TIMEOUT" --sources "$SOURCES" --reps "$REPS" --graphs "$GRAPHS" "$@"
)
echo "GLUON COMPARE COMPLETE $SLURM_JOB_ID"
