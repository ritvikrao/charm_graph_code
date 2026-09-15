#!/usr/bin/env bash
# 7.6g: reproduce the deadlock in the shipped default.
#
# Job 22071815 hung five baseline runs out of roughly twenty-six eligible at
# one node -- logv-frozen and control, --range-extend off, on mesh22 and
# mesh20. The condition is not reachable from the generator: --bucket-width 1
# on a 200x200 mesh puts every distance past the clamp but then refuses to
# coarsen (COARSEN_BAND_NARROW) and completes, and every width that does
# coarsen completes too in a single process. The stalled runs coarsened to
# bucket_scale 3-13 *and then* ran past the clamp, at eight processes of
# fifteen, so this reproduces the real configuration rather than a fixture.
#
# Runs are serial and each hang costs the full --timeout, so the walltime is
# sized for the worst case: every run hanging.
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
ROOT=${ACIC_BENCH_ROOT:-/scratch/mzu/rao1/acic-comparison-20260913}
GRAPH=${GRAPH:-mesh22}
REPEATS=${REPEATS:-8}
TIMEOUT=${TIMEOUT:-60}
OUT=${OUT:-$ROOT/logs/repro-$SLURM_JOB_ID}
mkdir -p "$OUT"

# The four sources the campaign used, so a hang here is the same event.
SOURCES=${SOURCES:-"737605 3030372 2105872 841388"}

hangs=0; runs=0
for rep in $(seq 1 "$REPEATS"); do
  for src in $SOURCES; do
    runs=$((runs + 1))
    log="$OUT/$GRAPH-$src-rep$rep.log"
    set +e
    srun -N 1 -n 8 --ntasks-per-node 8 -c 16 --cpu-bind=none --unbuffered \
      --kill-on-bad-exit=1 \
      bash "$APP/benchmarks/launch_acic.sh" 8 120 \
        "$ROOT/bin/acic_repro" 0 "$ROOT/graphs/$GRAPH.wsg" 1 "$src" 4 \
        0.999 0.005 --result-digest --timeout "$TIMEOUT" \
        --bucket-width-rule logv --clamp-freeze on \
        >"$log" 2>&1
    rc=$?
    set -e
    if grep -q '^PROGRESS_STALL' "$log"; then
      hangs=$((hangs + 1))
      echo "HANG  rep=$rep src=$src rc=$rc  $log"
    else
      echo "ok    rep=$rep src=$src rc=$rc"
      rm -f "$log"          # keep only the hangs; these logs are large
    fi
  done
done
echo "REPRO SUMMARY: $hangs hang(s) in $runs run(s) on $GRAPH -> $OUT"
