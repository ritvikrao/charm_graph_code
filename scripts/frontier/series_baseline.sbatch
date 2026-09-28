#!/usr/bin/env bash
# Gemini or HavoqGT for the scaling series (phase A), on the held-out sources
# through run.py --external-no-search, so rows, digest checks and timeouts
# match the other baselines.
#
#  ENGINE=gemini:  gemini_sssp, one rank per node (Gemini's design) x 56
#    OpenMP threads; input GRAPH.gemini32 beside the .wsg
#    (scripts/frontier/convert_gemini.sbatch).
#  ENGINE=havoqgt: havoqgt_sssp, 56 single-threaded ranks per node; each
#    graph is ingested into node-local /dev/shm for this layout first, and
#    removed after.
#
# ITEMS is GRAPH[:SOURCES[:TIMEOUT]],... in order (cheapest first: each
# graph's rows are written as it finishes). TIMEOUT caps one launch (run.py
# adds 90 s); a capped launch is recorded as a hang, and the speedup is then a
# lower bound.
#
#   ENGINE=gemini ITEMS=mesh28-z,grid3-30-z SOURCES=2 TIMEOUT=700 \
#       sbatch -N 16 scripts/frontier/series_baseline.sbatch CAMPAIGN
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=4
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-series-baseline
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign}
: "${ENGINE:?gemini or havoqgt}" "${ITEMS:?GRAPH[:SOURCES[:TIMEOUT]],...}"
SOURCES=${SOURCES:-2}
REPS=${REPS:-1}
TIMEOUT=${TIMEOUT:-700}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
NN=$SLURM_NNODES
SHM=/dev/shm/$USER-havoqgt-$SLURM_JOB_ID
printf 'job=%s hosts=%s engine=%s items=%s sources=%s reps=%s timeout=%s\n' \
  "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST" "$ENGINE" "$ITEMS" "$SOURCES" "$REPS" "$TIMEOUT"
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
export MPICH_OFI_NIC_POLICY=ROUND-ROBIN
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
sha256sum "$ROOT"/bin/{gemini_sssp,havoqgt_sssp,havoqgt_ingest}
clear_shm() { srun -N "$NN" --ntasks-per-node=1 --overlap rm -rf "$SHM" >/dev/null 2>&1; }
trap clear_shm EXIT
FAILED=
for item in ${ITEMS//,/ }; do
  IFS=: read -r graph sources timeout <<< "$item"
  sources=${sources:-$SOURCES} timeout=${timeout:-$TIMEOUT}
  echo "== $graph engine=$ENGINE sources=$sources timeout=$timeout $(date -Is)"
  case $ENGINE in
    gemini)
      if [ ! -s "$ROOT/graphs/$graph.gemini32" ] || [ ! -s "$ROOT/graphs/$graph.gemini32.info" ]; then
        echo "missing $graph.gemini32 (convert_gemini.sbatch)"; FAILED="$FAILED $graph"; continue
      fi
      extra=(--external-arms gemini --gemini-layouts 1 --gemini-binary gemini_sssp) ;;
    havoqgt)
      clear_shm
      start=$SECONDS
      srun -N "$NN" -n $((NN * 56)) --ntasks-per-node 56 -c 1 --cpu-bind=cores --unbuffered --kill-on-bad-exit=1 \
        --time=40 "$ROOT/bin/havoqgt_ingest" -O -o "$SHM/$graph" "$ROOT/graphs/$graph.wsg" \
        > "$ROOT/logs/series-havoqgt-ingest-$SLURM_JOB_ID-$graph.out" 2>&1
      rc=$?
      grep -E '^HAVOQGT_INGEST' "$ROOT/logs/series-havoqgt-ingest-$SLURM_JOB_ID-$graph.out"
      echo "ingest rc=$rc in $((SECONDS - start)) s; store $(srun -N 1 --ntasks-per-node=1 --overlap du -sh "$SHM" 2>/dev/null | cut -f1) on the first node"
      if [ $rc -ne 0 ]; then
        tail -5 "$ROOT/logs/series-havoqgt-ingest-$SLURM_JOB_ID-$graph.out"; FAILED="$FAILED $graph"; clear_shm; continue
      fi
      extra=(--external-arms havoqgt --havoqgt-store "$SHM") ;;
    *) echo "unknown ENGINE=$ENGINE"; exit 2 ;;
  esac
  "$PY" -u "$APP/benchmarks/run.py" "$ROOT" --mode external --external-no-search "${extra[@]}" \
    --workers 56 --timeout "$timeout" --sources "$sources" --reps "$REPS" --graphs "$graph" || FAILED="$FAILED $graph"
  [ "$ENGINE" = havoqgt ] && clear_shm
done
[ -z "$FAILED" ] || echo "failed:$FAILED"
echo "SERIES BASELINE COMPLETE $SLURM_JOB_ID"
