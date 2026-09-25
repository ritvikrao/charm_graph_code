#!/usr/bin/env bash
# One-node profile of ACIC against Wasp on one source, in the debug QOS.
# For each layout in LAYOUTS (ranks per node; 56 workers split evenly) the same
# source runs untraced, Projections-traced (ROOT/bin/$PRJ_BIN, no PAPI), with
# the work-cost counters (ROOT/bin/$COST_BIN) and with the per-round controller
# record (--diag); then Wasp runs the source at its tuned delta. Every binary is
# built from one revision (build_onenode.sh ... production / projections /
# work-cost), so the traced and counted runs execute the timed run's code.
#
#   sbatch scripts/frontier/profile_1n.sbatch CAMPAIGN mesh28-z 141442404 "FLAGS"
#   LAYOUTS="8 1" RUNS="plain plain trace plain cost diag plain" WASP_DELTA=4096
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --qos=debug
#SBATCH --nodes=1
#SBATCH --time=01:30:00
#SBATCH --job-name=acic-profile-1n
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign root}
GRAPH=${2:?graph}
SOURCE=${3:?source}
FLAGS=${4:-}
BASE=${BASE:-acic_1n}
PRJ_BIN=${PRJ_BIN:-acic_1n_prj}
COST_BIN=${COST_BIN:-acic_1n_cost}
LAYOUTS=${LAYOUTS:-8 1}
RUNS=${RUNS:-plain plain trace plain cost diag plain}
WASP_DELTA=${WASP_DELTA:-4096}
WASP_REPS=${WASP_REPS:-4}
LOGSIZE=${LOGSIZE:-8000000}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
WORKERS=56
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
OUT=$ROOT/profile/$GRAPH-1n-$SLURM_JOB_ID
mkdir -p "$OUT"
printf 'job=%s hosts=%s source=%s\nflags=%s\nlayouts=%s runs=%s logsize=%s\n' "$SLURM_JOB_ID" \
  "$SLURM_JOB_NODELIST" "$SOURCE" "$FLAGS" "$LAYOUTS" "$RUNS" "$LOGSIZE" | tee "$OUT/job.txt"
sha256sum "$ROOT/bin/$BASE" "$ROOT/bin/$PRJ_BIN" "$ROOT/bin/$COST_BIN" "$ROOT/bin/wasp_sssp" | tee -a "$OUT/job.txt"
EXPECT=$(awk -F'\t' -v s="$SOURCE" '$1 == s {print "h1=" $3 " h2=" $4 " reachable=" $5 " distance_sum=" $6}' \
  "$ROOT/graphs/$GRAPH.reference.txt")
echo "expected $EXPECT" | tee -a "$OUT/job.txt"

check() {  # check OUTFILE -> VALID/INVALID against the reference row
  local got
  got=$(grep -m1 -o 'h1=[0-9]* h2=[0-9]* reachable=[0-9]* distance_sum=[0-9]*' "$1")
  [ -n "$got" ] && [ "$got" = "$EXPECT" ] && echo VALID || echo INVALID
}

launch() {  # launch RPN BINARY LABEL [extra args...]
  local rpn=$1 bin=$2 label=$3; shift 3
  local per=$((WORKERS / rpn))
  srun -N 1 -n "$rpn" --ntasks-per-node "$rpn" -c "$per" --network=single_node_vni \
       --cpu-bind=none --unbuffered --kill-on-bad-exit=1 \
       bash "$APP/benchmarks/launch_acic.sh" "$rpn" "$WORKERS" "$bin" \
       0 "$ROOT/graphs/$GRAPH.wsg" 1 "$SOURCE" 4 0.999 0.005 \
       --result-digest --timeout 300 $FLAGS "$@" > "$OUT/$label.out" 2>&1 || echo "$label: exit $?"
  printf '%-16s %s | reductions %s | %s\n' "$label" \
    "$(grep -m1 -E '^Compute time' "$OUT/$label.out" | tr -s ' ')" \
    "$(grep -m1 -oE 'Number of reductions: [0-9]+' "$OUT/$label.out" | grep -oE '[0-9]+$')" \
    "$(check "$OUT/$label.out")" | tee -a "$OUT/summary.txt"
}

for rpn in $LAYOUTS; do
  per=$((WORKERS / rpn))
  i=0
  for kind in $RUNS; do
    i=$((i + 1))
    label=$(printf '%sx%s-%02d-%s' "$rpn" "$per" "$i" "$kind")
    case $kind in
      plain) launch "$rpn" "$ROOT/bin/$BASE" "$label" ;;
      diag)
        mkdir -p "$OUT/$label.diag"
        ACIC_DIAG_DIR="$OUT/$label.diag" launch "$rpn" "$ROOT/bin/$BASE" "$label" ;;
      cost)
        launch "$rpn" "$ROOT/bin/$COST_BIN" "$label"
        grep -E '^(WORK_COST|WORK_CLOCK)' "$OUT/$label.out" | tee -a "$OUT/summary.txt" ;;
      trace)
        mkdir -p "$OUT/$label.trace"
        launch "$rpn" "$ROOT/bin/$PRJ_BIN" "$label" +traceoff +logsize "$LOGSIZE" +traceroot "$OUT/$label.trace"
        du -sh "$OUT/$label.trace" | tee -a "$OUT/summary.txt" ;;
      *) echo "unknown run kind $kind" >&2; exit 2 ;;
    esac
  done
done

# Wasp on the same source and node, at its tuned delta (run.py's command line).
for rep in $(seq 1 "$WASP_REPS"); do
  w=$OUT/wasp-d$WASP_DELTA-$rep.out
  srun -N 1 -n 1 --ntasks-per-node 1 -c 56 --cpu-bind=cores --unbuffered --kill-on-bad-exit=1 \
    "$ROOT/bin/wasp_sssp" "$ROOT/graphs/$GRAPH.wsg" "$SOURCE" "$WASP_DELTA" > "$w" 2>&1 || echo "wasp $rep: exit $?"
  printf 'wasp-d%s-%s     %s | %s\n' "$WASP_DELTA" "$rep" "$(grep -m1 -oE 'solve_seconds=[0-9.]+' "$w")" \
    "$(check "$w")" | tee -a "$OUT/summary.txt"
done

# Trace reports (projections_report.py reads the logs without the GUI).
for t in "$OUT"/*-trace.trace; do
  [ -d "$t" ] || continue
  label=$(basename "$t" .trace)
  rpn=${label%%x*}
  "$PY" "$APP/benchmarks/projections_report.py" "$t/$PRJ_BIN" \
      --pes-per-process $((WORKERS / rpn)) --processes-per-node "$rpn" --jobs 56 \
      --json "$OUT/$label-report.json" > "$OUT/$label-report.md" 2>"$OUT/$label.report.err" || echo "$label: report failed"
done
echo "PROFILE COMPLETE $SLURM_JOB_ID output in $OUT"
