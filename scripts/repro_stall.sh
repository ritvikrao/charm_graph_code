#!/usr/bin/env bash
# 7.6g: test an explanation of the default deadlock, paired, at the recorded
# configuration. ARM_FLAG names the solver flag under test and ARMS its values;
# the two explanations tested so far are --skew-defer (refuted: it changed
# nothing) and --pq-overflow-last (the default below).
#
# The history of the first explanation is kept here because it is why the
# script is paired:
#
# design/step76-default-deadlock.md leaves one question open: why do the last
# few in-window updates, at their own admission threshold, never retire? Every
# recorded stall pinned at exactly floor(2047 / bucket_scale), the top real
# bucket, which is where a count lands when a receiver still at scale 1 charges
# an unflagged update to index 2047 -- the index coarsen_buckets() leaves out
# of the merge -- after its creator has already coarsened. See --skew-defer in
# sssp_smp.cpp.
#
# That explanation makes two predictions this script checks at once, on the
# same node and the same sources, with the arms interleaved:
#   1. --skew-defer off: a run hangs only if it reports skewed top-bucket
#      arrivals, and runs that report them hang.
#   2. --skew-defer on: no hangs, and every digest matches the independent
#      reference.
# The rescue is disabled explicitly (--stall-rescue 0): with it on, a hang is
# rescued before PROGRESS_STALL is printed and this would measure nothing.
#
# Writes one TSV row per run; keeps full logs only for hangs and mismatches.
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
ROOT=${ACIC_BENCH_ROOT:?campaign directory with bin/ and graphs/}
BIN=${BIN:-$ROOT/bin/acic_g2}
ARM_FLAG=${ARM_FLAG:---pq-overflow-last}
GRAPH=${GRAPH:-mesh22}
REPEATS=${REPEATS:-10}
TIMEOUT=${TIMEOUT:-45}
ARMS=${ARMS:-"off on"}
RPN=${RPN:-8}
WORKERS=${WORKERS:-120}
CORES=${CORES:-128}
OUT=${OUT:-$ROOT/logs/stall-${ARM_FLAG#--}-$GRAPH-${SLURM_JOB_ID:-local}}
mkdir -p "$OUT"
REF="$ROOT/graphs/$GRAPH.reference.txt"
# The width campaign's held-out sources: reference rows 3-6, as run.py's
# refs[2:2+4]. A hang here is then the same event the campaign recorded.
SOURCES=${SOURCES:-$(awk -F'\t' '$2=="test"{print $1}' "$REF" | head -4 | tr '\n' ' ')}
TSV="$OUT/runs.tsv"
[ -s "$TSV" ] || printf 'graph\tflag\tarm\trep\tsource\trc\thung\tskew_arrivals\tstall_rescues\tbucket_scale\tcompute_seconds\tdigest_ok\n' > "$TSV"
printf 'bin %s\n' "$(sha256sum "$BIN")" > "$OUT/manifest.txt"
printf 'host %s job %s\n' "$(hostname)" "${SLURM_JOB_ID:-}" >> "$OUT/manifest.txt"

for rep in $(seq 1 "$REPEATS"); do
  for src in $SOURCES; do
    # Alternate which arm goes first, so neither always follows a hang.
    arms=$ARMS
    [ $((rep % 2)) -eq 0 ] && arms=$(echo "$ARMS" | awk '{for(i=NF;i>0;i--) printf "%s ", $i}')
    for arm in $arms; do
      log="$OUT/$GRAPH-$arm-$src-rep$rep.log"
      set +e
      srun -N 1 -n "$RPN" --ntasks-per-node "$RPN" -c $((CORES / RPN)) --cpu-bind=none \
        --unbuffered --kill-on-bad-exit=1 \
        bash "$APP/benchmarks/launch_acic.sh" "$RPN" "$WORKERS" \
          "$BIN" 0 "$ROOT/graphs/$GRAPH.wsg" 1 "$src" 4 0.999 0.005 \
          --result-digest --timeout "$TIMEOUT" --stall-rescue 0 \
          --bucket-width-rule logv --clamp-freeze on "$ARM_FLAG" "$arm" \
          >"$log" 2>&1
      rc=$?
      set -e
      hung=0
      grep -qE '^PROGRESS_STALL|^TIMEOUT' "$log" && hung=1
      skew=$(sed -n 's/^Skewed top-bucket arrivals: \([0-9]*\).*/\1/p' "$log" | head -1)
      rescues=$(sed -n 's/^Stall rescues: \([0-9]*\)/\1/p' "$log" | head -1)
      scale=$(sed -n 's/^Bucket scale: \([0-9]*\).*/\1/p' "$log" | head -1)
      secs=$(sed -n 's/^Compute time: \(.*\)/\1/p' "$log" | head -1)
      got=$(sed -n 's/^VERIFY parallel digest h1=\([0-9]*\) h2=\([0-9]*\).*/\1 \2/p' "$log" | head -1)
      want=$(awk -F'\t' -v s="$src" '$1==s{print $3" "$4}' "$REF")
      ok=0; [ -n "$got" ] && [ "$got" = "$want" ] && ok=1
      printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$GRAPH" "$ARM_FLAG" "$arm" "$rep" "$src" "$rc" "$hung" \
        "${skew:-NA}" "${rescues:-NA}" "${scale:-NA}" "${secs:-NA}" "$ok" >> "$TSV"
      echo "$GRAPH arm=$arm rep=$rep src=$src rc=$rc hung=$hung skew=${skew:-NA} scale=${scale:-NA} t=${secs:-NA} ok=$ok"
      if [ "$hung" -eq 0 ] && [ "$ok" -eq 1 ]; then
        rm -f "$log"
      else
        gzip -f "$log"
      fi
    done
  done
done
echo "REPRO STALL COMPLETE $OUT"
