#!/usr/bin/env bash
# Hardware-counter profile of one ACIC configuration (acic_prof.h), with
# untraced runs of the same source interleaved, so what the profile costs is
# measured in the same allocation (RUNS below). Profiles cover the solve only
# (start_papi() .. print_distances()).
#
# Build the profile binary first (module load papi; make sssp_smp_papi) and
# stage it as ROOT/bin/acic_papi. BASE (default acic) is the untraced binary.
# The launch matches run.py's ACIC launches (launch_acic.sh, same flags and
# environment).
#
#   sbatch -N 2 scripts/anvil/papi_profile.sbatch \
#       /anvil/scratch/$USER/acic/campaign rmat25 23077392 8 [BASE] [PROF]
#   RUNS="plain off ev1 counted plain" sbatch ...   # isolate the overhead
#   EXTRA="--bufsize 6144" sbatch ...               # solver flags for every run
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-papi
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign root}
GRAPH=${2:?graph}
SOURCE=${3:?source}
RPN=${4:-8}
BASE=${5:-acic}
PROF=${6:-acic_papi}
WORKERS=${WORKERS:-120}  # per node; 112 for 16 x 7
module load gcc/11.2.0 openmpi/4.0.6 libfabric/1.12.0 numactl/2.0.14 hwloc/1.11.13 python/3.9.5 papi/6.0.0.1 >/dev/null 2>&1
export SLURM_MPI_TYPE=pmi2 PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid \
       NO_AFFINITY=1 OMP_PROC_BIND=close OMP_PLACES=cores
# papi/6.0.0.1's libpfm4 does not know Zen 3; see acic_prof.h.
export LIBPFM_FORCE_PMU=amd64_fam17h_zen2
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
N=$SLURM_NNODES
OUT=$ROOT/papi/$GRAPH-${N}n-${RPN}x$((WORKERS / RPN))-$PROF-$SLURM_JOB_ID
mkdir -p "$OUT"
printf 'job=%s hosts=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST"
sha256sum "$ROOT/bin/$BASE" "$ROOT/bin/$PROF"
papi_avail 2>&1 | grep -E 'Number Hardware Counters|Model string'
cat /proc/sys/kernel/perf_event_paranoid

launch() {  # launch BINARY LABEL [charm flags...]
  local bin=$1 label=$2; shift 2
  # Anvil sometimes fails a step launch outright ("Communication connection
  # failure"); that is the allocation, not the run, so try once more.
  local try
  for try in 1 2; do
    srun -N "$N" -n $((N * RPN)) --ntasks-per-node "$RPN" -c $((128 / RPN)) \
         --cpu-bind=none --unbuffered --kill-on-bad-exit=1 \
         bash "$APP/benchmarks/launch_acic.sh" "$RPN" "$WORKERS" "$bin" \
         0 "$ROOT/graphs/$GRAPH.wsg" 1 "$SOURCE" 4 0.999 0.005 \
         --result-digest --timeout 300 ${EXTRA:-} "$@" > "$OUT/$label.out" 2>&1 && break
    grep -q 'Task launch for' "$OUT/$label.out" && [ $try -eq 1 ] || return 1
    echo "$label: step launch failed, retrying"
  done
  printf '%-10s %s | %s | %s\n' "$label" \
    "$(grep -E '^Compute time' "$OUT/$label.out" | tr -s ' ')" \
    "$(grep -E '^Insts per edge' "$OUT/$label.out")" \
    "$(grep -E 'VERIFY parallel digest' "$OUT/$label.out" | cut -c1-80)"
}

grep -E "^$SOURCE\b" "$ROOT/graphs/$GRAPH.reference.txt" || true
# RUNS picks the sequence; each word is one launch in this allocation:
#   plain    the untraced binary
#   timer    the profile binary, sampling on thread CPU time, no counters
#   sampled  the profile binary, counting and sampling on cycles
#   counted  the profile binary, counting only
#   evK      the profile binary, counting only the first K events
#   off      the profile binary with PAPI skipped (context switches only)
RUNS=${RUNS:-warmup plain sampled plain counted plain}
i=0
for kind in $RUNS; do
  i=$((i + 1))
  label=$(printf '%02d-%s' "$i" "$kind")
  dir="$OUT/$label.prof"
  mkdir -p "$dir"
  case $kind in
    warmup|plain) launch "$ROOT/bin/$BASE" "$label" ;;
    sampled) ACIC_PROF_DIR="$dir" ACIC_PROF_PERIOD=1000003 launch "$ROOT/bin/$PROF" "$label" ;;
    timer) ACIC_PROF_DIR="$dir" ACIC_PROF_TIMER=${ACIC_PROF_TIMER:-250} launch "$ROOT/bin/$PROF" "$label" ;;
    counted) ACIC_PROF_DIR="$dir" ACIC_PROF_PERIOD=0 launch "$ROOT/bin/$PROF" "$label" ;;
    ev*) ACIC_PROF_DIR="$dir" ACIC_PROF_PERIOD=0 ACIC_PROF_EVENTS=${kind#ev} \
           launch "$ROOT/bin/$PROF" "$label" ;;
    off) ACIC_PROF_DIR="$dir" ACIC_PROF_OFF=1 launch "$ROOT/bin/$PROF" "$label" ;;
    *) echo "unknown run kind $kind" >&2; exit 2 ;;
  esac
  if [ -n "$(ls "$dir")" ]; then
    python3 "$APP/benchmarks/papi_profile_report.py" "$dir" "$ROOT/bin/$PROF" \
        --run-output "$OUT/$label.out" > "$OUT/$label.md" || true
    sed -n '/^## Counters/,/^## Samples by function/p' "$OUT/$label.md" | grep '^| [a-z*]'
  else
    rmdir "$dir"
  fi
done
echo "output in $OUT"
