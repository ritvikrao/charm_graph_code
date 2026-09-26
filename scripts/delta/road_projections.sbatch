#!/usr/bin/env bash
# Trace the frozen one-node road time profile with matched untraced controls.
# Usage: sbatch --output=ROOT/logs/road-projections-%j.out \
#   scripts/delta/road_projections.sbatch ROOT [SOURCE]
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-road-prj
set -euo pipefail
ulimit -c 0

ROOT=${1:?campaign directory}
SOURCE=${2:-8718204}  # held-out source in road-usa-z.reference.txt
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
GRAPH=road-usa-z
PLAIN=$ROOT/bin/acic_full_65536
TRACED=acic_full_65536_prj_$SLURM_JOB_ID
OUT=$ROOT/traces/$GRAPH-$SOURCE-$SLURM_JOB_ID
mkdir -p "$OUT"

export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_PROF_TIMER ACIC_PROF_OFF ACIC_PROF_DIR ACIC_PREBIND ACIC_LAUNCH_PREFIX

# The accepted binary was built from 551571d with these exact application
# defines. build_onenode.sh puts its source tree, build log and manifest on HDD.
ACIC_BUILD_FLAGS='-DACIC_PROCESS_CHUNKS -DACIC_CHUNK_DISTANCE_WIDTH=65536' \
  bash "$APP/scripts/delta/build_onenode.sh" "$ROOT" "$TRACED" 551571d projections
sha256sum "$PLAIN" "$ROOT/bin/$TRACED" > "$OUT/binaries.sha256"
cp "$ROOT/bin/$TRACED.manifest" "$OUT/"
printf 'job=%s host=%s source=%s output=%s\n' \
  "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST" "$SOURCE" "$OUT"

FLAGS=(--process-share auto --reader-tile auto --slack-control off
       --process-queue nearest --process-queue-batch 8 --hub-hints auto
       +old-scheduler --bucket-width 131072 --heap-slice 64)
launch() {
  local label=$1 binary=$2
  shift 2
  printf 'run=%s binary=%s\n' "$label" "$binary"
  srun -N1 -n16 --ntasks-per-node=16 -c8 --cpu-bind=none --unbuffered \
       --kill-on-bad-exit=1 --time=12 \
       bash "$APP/benchmarks/launch_acic.sh" 16 112 "$binary" \
       0 "$ROOT/graphs/$GRAPH.wsg" 1 "$SOURCE" 4 0.999 0.005 \
       --result-digest --timeout 600 "${FLAGS[@]}" "$@" \
       > "$OUT/$label.out" 2>&1
  python3 "$APP/benchmarks/check_onenode_digest.py" \
    "$ROOT/graphs/$GRAPH.reference.txt" "$SOURCE" "$OUT/$label.out"
  if grep -q 'Projections log flushed to disk' "$OUT/$label.out"; then
    echo 'Trace buffer flushed during solve; inspect trace overhead.' >&2
    exit 1
  fi
  grep -E '^Compute time:|^VERIFY parallel digest' "$OUT/$label.out"
}

launch warmup "$PLAIN"
launch plain-before "$PLAIN"
launch traced "$ROOT/bin/$TRACED" +traceoff +logsize 8000000 +traceroot "$OUT"
launch plain-after "$PLAIN"

python3 "$APP/benchmarks/projections_report.py" "$OUT/$TRACED" \
  --pes-per-process 7 --processes-per-node 16 --bin-ms 50 --jobs 16 \
  --json "$OUT/projections.json" > "$OUT/projections.md"
du -sh "$OUT"
printf 'COMPLETE %s\n' "$OUT"
