#!/usr/bin/env bash
# Gluon-Async for the scaling series, pinned: a search does not fit a two-hour
# job when one solve takes many minutes. Every graph runs 8 ranks per node, oec,
# and its own delta, on the held-out sources (run.py --external-no-search).
#
# The delta is the meshes' tuned 64 (8 ranks, oec, selected on mesh24-z and
# mesh26-z) scaled by the typical edge weight, since Gluon raises its priority
# threshold by delta every round: 64 for weights uniform over [1, 1000] (mean
# 500), 1 for [1, 10], 4096 for [1, 65536], and 128 for terrain (flat ground
# costs 661-935 per edge).
#
# ITEMS is GRAPH:DELTA:BINARY[:SOURCES[:TIMEOUT]],... (BINARY gluon_sssp, or
# gluon_sssp64 for 2^32+ vertices; SOURCES and TIMEOUT default to the variables
# of those names), run in order, so put the cheapest first: each graph's rows
# are written as it finishes. TIMEOUT caps one launch (graph load and solve;
# run.py adds 90 s); a launch past it is recorded as a hang, and the graph's
# speedup is then a lower bound.
#
#   ITEMS=mesh28-z:64:gluon_sssp,mesh32-z:64:gluon_sssp64:1:3000 SOURCES=2 TIMEOUT=900 \
#       sbatch -N 16 scripts/frontier/series_gluon.sbatch CAMPAIGN
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=16
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-series-gluon
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign}
: "${ITEMS:?GRAPH:DELTA:BINARY,...}"
SOURCES=${SOURCES:-2}
REPS=${REPS:-1}
TIMEOUT=${TIMEOUT:-1800}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
printf 'job=%s hosts=%s items=%s sources=%s reps=%s timeout=%s\n' \
  "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST" "$ITEMS" "$SOURCES" "$REPS" "$TIMEOUT"
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
export MPICH_OFI_NIC_POLICY=ROUND-ROBIN
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
sha256sum "$ROOT/bin/gluon_sssp" "$ROOT/bin/gluon_sssp64"
FAILED=
for item in ${ITEMS//,/ }; do
  IFS=: read -r graph delta binary sources timeout <<< "$item"
  sources=${sources:-$SOURCES} timeout=${timeout:-$TIMEOUT}
  echo "== $graph delta=$delta binary=$binary sources=$sources timeout=$timeout"
  "$PY" -u "$APP/benchmarks/run.py" "$ROOT" --mode external --external-no-search --external-arms gluon-async \
    --gluon-binary "$binary" --gluon-layouts 8 --gluon-partitions oec --gluon-deltas "$delta" \
    --workers 56 --timeout "$timeout" --sources "$sources" --reps "$REPS" --graphs "$graph" || FAILED="$FAILED $graph"
done
[ -z "$FAILED" ] || echo "run.py failed:$FAILED"
echo "SERIES GLUON COMPLETE $SLURM_JOB_ID"
