#!/usr/bin/env bash
# Matched Projections of the frozen baseline ACIC on road-USA and mesh24.
# Usage: sbatch --output=CAMPAIGN/road-gap-trace-%j.out road_gap_trace.sbatch CAMPAIGN
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-gap-trace
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign directory}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
GRAPHS=/work/hdd/mzu/rao1/acic-ipdps27-onenode-20260919/graphs
PLAIN=/work/hdd/mzu/rao1/acic-road-opt-20260925/bin/acic_base
TRACED=acic_base_gap_prj_$SLURM_JOB_ID
OUT=$ROOT/traces/baseline-$SLURM_JOB_ID
mkdir -p "$OUT"
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_PROF_TIMER ACIC_PROF_OFF ACIC_PROF_DIR ACIC_PREBIND ACIC_LAUNCH_PREFIX
bash "$APP/scripts/delta/build_onenode.sh" "$ROOT" "$TRACED" 87f04af projections
sha256sum "$PLAIN" "$ROOT/bin/$TRACED" > "$OUT/binaries.sha256"
cp "$ROOT/bin/$TRACED.manifest" "$OUT/"
printf 'job=%s host=%s output=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST" "$OUT"

BASE=(--process-share auto --reader-tile auto --slack-control off
      --process-queue nearest --process-queue-batch 8 --hub-hints auto
      +old-scheduler --heap-slice 8)
launch() {
  local graph=$1 source=$2 label=$3 binary=$4
  shift 4
  local flags=("${BASE[@]}")
  if [ "$graph" = road-usa-z ]; then flags+=(--bucket-width 131072); fi
  srun -N1 -n16 --ntasks-per-node=16 -c8 --cpu-bind=none --unbuffered \
       --kill-on-bad-exit=1 --time=12 \
       bash "$APP/benchmarks/launch_acic.sh" 16 112 "$binary" \
       0 "$GRAPHS/$graph.wsg" 1 "$source" 4 0.999 0.005 \
       --result-digest --timeout 600 "${flags[@]}" "$@" \
       > "$OUT/$graph/$label.out" 2>&1
  python3 "$APP/benchmarks/check_onenode_digest.py" \
    "$GRAPHS/$graph.reference.txt" "$source" "$OUT/$graph/$label.out"
  if grep -q 'Projections log flushed to disk' "$OUT/$graph/$label.out"; then
    echo "Trace buffer flushed for $graph" >&2
    exit 1
  fi
  printf '%s %s ' "$graph" "$label"
  grep -E '^Compute time:' "$OUT/$graph/$label.out"
}

for graph in road-usa-z mesh24-z; do
  source=8718204
  if [ "$graph" = mesh24-z ]; then source=4910385; fi
  mkdir -p "$OUT/$graph"
  launch "$graph" "$source" warmup "$PLAIN"
  launch "$graph" "$source" plain-before "$PLAIN"
  launch "$graph" "$source" traced "$ROOT/bin/$TRACED" \
    +traceoff +logsize 8000000 +traceroot "$OUT/$graph"
  launch "$graph" "$source" plain-after "$PLAIN"
  python3 "$APP/benchmarks/projections_report.py" "$OUT/$graph/$TRACED" \
    --pes-per-process 7 --processes-per-node 16 --bin-ms 50 --jobs 16 \
    --json "$OUT/$graph/projections.json" > "$OUT/$graph/projections.md"
done
du -sh "$OUT"
printf 'COMPLETE %s\n' "$OUT"
