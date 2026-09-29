#!/usr/bin/env bash
# RIKEN (Graph500 SSSP, riken_driver.cpp) for the scaling series (F6), pinned:
# a search does not fit a two-hour job when one solve takes many minutes.
# RIKEN reads narrow .wsg files only (at most 2^31 - 1 vertices), and its
# binary32 distances are exact only below 2^24, so the series inputs it can
# run are terrain30-s-z, grid3-30-z and mesh28-w10-z (mesh28-z already has
# RIKEN rows at 16 and 64 nodes).
#
# Every earlier RIKEN search on the meshes chose 8 ranks per node and delta =
# denominator, about twice the mean edge weight (5536476, 5541663, 5541664,
# 5541371). ITEMS carries that ratio per graph: GRAPH:DELTA[:SOURCES[:TIMEOUT]]
# with DELTA the numerator over the graph's riken_denominator. RPN (default 8)
# sets the layout. TIMEOUT caps one launch (run.py adds 90 s); a capped launch
# is recorded as a hang, and the speedup is then a lower bound.
#
#   ITEMS=grid3-30-z:1024,mesh28-w10-z:16,terrain30-s-z:2048::1500 SOURCES=2 TIMEOUT=700 \
#       sbatch -N 16 scripts/frontier/series_riken.sbatch CAMPAIGN
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=16
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-series-riken
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign}
: "${ITEMS:?GRAPH:DELTA,...}"
SOURCES=${SOURCES:-2}
REPS=${REPS:-1}
TIMEOUT=${TIMEOUT:-700}
RPN=${RPN:-8}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
printf 'job=%s hosts=%s items=%s sources=%s reps=%s timeout=%s rpn=%s\n' \
  "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST" "$ITEMS" "$SOURCES" "$REPS" "$TIMEOUT" "$RPN"
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
export MPICH_OFI_NIC_POLICY=ROUND-ROBIN
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
sha256sum "$ROOT/bin/riken_sssp"
FAILED=
for item in ${ITEMS//,/ }; do
  IFS=: read -r graph delta sources timeout <<< "$item"
  sources=${sources:-$SOURCES} timeout=${timeout:-$TIMEOUT}
  echo "== $graph delta=$delta rpn=$RPN sources=$sources timeout=$timeout $(date -Is)"
  "$PY" -u "$APP/benchmarks/run.py" "$ROOT" --mode external --external-no-search --external-arms riken \
    --riken-layouts "$RPN" --riken-delta "$delta" \
    --workers 56 --timeout "$timeout" --sources "$sources" --reps "$REPS" --graphs "$graph" || FAILED="$FAILED $graph"
done
[ -z "$FAILED" ] || echo "run.py failed:$FAILED"
echo "SERIES RIKEN COMPLETE $SLURM_JOB_ID"
