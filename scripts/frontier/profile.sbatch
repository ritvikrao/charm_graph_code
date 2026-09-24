#!/usr/bin/env bash
# Frontier profile of one ACIC configuration on one source: untraced launches
# interleaved with PC-sampled (acic_prof.h timer mode, solve window only) and
# Projections-traced launches, so each profile's cost is measured in the same
# allocation; then RIKEN's relaxation count (riken_sssp_verbose) on the same
# source and node count. Merges scripts/anvil/papi_profile.sbatch and
# trace_projections.sbatch, with Frontier's environment (onenode_ab.sbatch).
#
# Build the profile binaries first (scripts/delta/build_onenode.sh ... papi /
# projections) as ROOT/bin/acic_papi and ROOT/bin/acic_prj.
#
#   sbatch -N 16 scripts/frontier/profile.sbatch CAMPAIGN rmat26 29743083 4 \
#       "--process-share auto ... --heap-slice 8"
#   RUNS="warmup plain timer plain trace plain" RIKEN="8 16"   # defaults
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=16
#SBATCH --time=00:40:00
#SBATCH --job-name=acic-profile
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign root}
GRAPH=${2:?graph}
SOURCE=${3:?source}
RPN=${4:?ranks per node}
FLAGS=${5:-}
BASE=${BASE:-acic_slice}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
N=$SLURM_NNODES
WORKERS=56
PER=$((WORKERS / RPN))
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
OUT=$ROOT/profile/$GRAPH-${N}n-${RPN}x$PER-$SLURM_JOB_ID
mkdir -p "$OUT"
printf 'job=%s hosts=%s\nflags=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST" "$FLAGS" | tee "$OUT/job.txt"
sha256sum "$ROOT/bin/$BASE" "$ROOT/bin/acic_papi" "$ROOT/bin/acic_prj" "$ROOT/bin/riken_sssp_verbose" 2>/dev/null | tee -a "$OUT/job.txt"
grep -E "^$SOURCE\b" "$ROOT/graphs/$GRAPH.reference.txt" | tee -a "$OUT/job.txt" || true
EXTRA=()
[ "$N" = 1 ] && EXTRA=(--network=single_node_vni)

launch() {  # launch BINARY LABEL [charm flags...]
  local bin=$1 label=$2; shift 2
  srun -N "$N" -n $((N * RPN)) --ntasks-per-node "$RPN" -c "$PER" "${EXTRA[@]}" \
       --cpu-bind=none --unbuffered --kill-on-bad-exit=1 \
       bash "$APP/benchmarks/launch_acic.sh" "$RPN" "$WORKERS" "$bin" \
       0 "$ROOT/graphs/$GRAPH.wsg" 1 "$SOURCE" 4 0.999 0.005 \
       --result-digest --timeout 120 $FLAGS "$@" > "$OUT/$label.out" 2>&1 || echo "$label: exit $?"
  printf '%-10s %s | %s\n' "$label" \
    "$(grep -E '^Compute time' "$OUT/$label.out" | tr -s ' ')" \
    "$(grep -E 'VERIFY parallel digest' "$OUT/$label.out" | cut -c1-90)"
}

RUNS=${RUNS:-warmup plain timer plain trace plain timer plain}
i=0
for kind in $RUNS; do
  i=$((i + 1))
  label=$(printf '%02d-%s' "$i" "$kind")
  case $kind in
    warmup|plain) launch "$ROOT/bin/$BASE" "$label" ;;
    timer)
      mkdir -p "$OUT/$label.prof"
      ACIC_PROF_DIR="$OUT/$label.prof" ACIC_PROF_TIMER=${ACIC_PROF_TIMER:-100} launch "$ROOT/bin/acic_papi" "$label"
      "$PY" "$APP/benchmarks/papi_profile_report.py" "$OUT/$label.prof" "$ROOT/bin/acic_papi" \
          --run-output "$OUT/$label.out" > "$OUT/$label.md" 2>"$OUT/$label.report.err" || echo "$label: report failed" ;;
    trace)
      mkdir -p "$OUT/$label.trace"
      launch "$ROOT/bin/acic_prj" "$label" +traceoff +traceroot "$OUT/$label.trace"
      du -sh "$OUT/$label.trace"
      "$PY" "$APP/benchmarks/projections_report.py" "$OUT/$label.trace/acic_prj" \
          --pes-per-process "$PER" --processes-per-node "$RPN" --jobs 56 \
          --json "$OUT/$label-report.json" > "$OUT/$label-report.md" 2>"$OUT/$label.report.err" || echo "$label: report failed" ;;
    *) echo "unknown run kind $kind" >&2; exit 2 ;;
  esac
done

# RIKEN's relaxations sent on the same source (its selected layout and delta).
read -r RR RD <<< "${RIKEN:-8 16}"
if [ -n "$RR" ] && [ -x "$ROOT/bin/riken_sssp_verbose" ]; then
  DEN=$(sed -n 's/.*riken_denominator=\([0-9]*\).*/\1/p' "$ROOT/graphs/$GRAPH.meta")
  (
    module load boost/1.85.0 >/dev/null 2>&1
    module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
    export MPICH_OFI_NIC_POLICY=ROUND-ROBIN OMP_NUM_THREADS=$((WORKERS / RR))
    for rep in 1 2; do
      srun -N "$N" -n $((N * RR)) --ntasks-per-node "$RR" -c $((WORKERS / RR)) --cpu-bind=cores \
           --unbuffered --kill-on-bad-exit=1 "$ROOT/bin/riken_sssp_verbose" \
           "$ROOT/graphs/$GRAPH.wsg" "$SOURCE" "$RD" "$DEN" 0 > "$OUT/riken-$rep.out" 2>&1 || echo "riken $rep: exit $?"
      grep -E 'RELAX_SENT|BENCH' "$OUT/riken-$rep.out" | cut -c1-160
    done
  )
fi
echo "PROFILE COMPLETE $SLURM_JOB_ID output in $OUT"
