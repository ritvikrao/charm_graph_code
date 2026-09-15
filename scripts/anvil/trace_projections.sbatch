#!/usr/bin/env bash
# Projections traces of one ACIC configuration, with untraced runs of the same
# source interleaved (U T U T U) so tracing overhead is measured in the same
# allocation. Tracing covers the solve only: the binary runs with +traceoff and
# the solver calls traceBegin() when compute starts and traceEnd() at the end.
#
# Build the traced binary first (make sssp_smp_projections) and stage it as
# ROOT/bin/acic_prj. The launch matches run.py's ACIC launches (launch_acic.sh,
# same flags and environment).
#
#   sbatch -N 2 scripts/anvil/trace_projections.sbatch \
#       /anvil/scratch/$USER/acic/campaign rmat25 23077392 8
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-trace
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign root}
GRAPH=${2:?graph}
SOURCE=${3:?source}
RPN=${4:-8}
WORKERS=120
module load gcc/11.2.0 openmpi/4.0.6 libfabric/1.12.0 numactl/2.0.14 hwloc/1.11.13 python/3.9.5 >/dev/null 2>&1
export SLURM_MPI_TYPE=pmi2 PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid \
       NO_AFFINITY=1 OMP_PROC_BIND=close OMP_PLACES=cores
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
N=$SLURM_NNODES
OUT=$ROOT/traces/$GRAPH-${N}n-${RPN}x$((WORKERS / RPN))-$SLURM_JOB_ID
mkdir -p "$OUT"
printf 'job=%s hosts=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST"
sha256sum "$ROOT/bin/acic" "$ROOT/bin/acic_prj"

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
         --result-digest --timeout 300 "$@" > "$OUT/$label.out" 2>&1 && break
    grep -q 'Task launch for' "$OUT/$label.out" && [ $try -eq 1 ] || return 1
    echo "$label: step launch failed, retrying"
  done
  printf '%-10s %s | %s\n' "$label" \
    "$(grep -E '^Compute time' "$OUT/$label.out" | tr -s ' ')" \
    "$(grep -E 'VERIFY parallel digest' "$OUT/$label.out" | cut -c1-80)"
}

grep -E "^$SOURCE\b" "$ROOT/graphs/$GRAPH.reference.txt" || true
launch "$ROOT/bin/acic" warmup
launch "$ROOT/bin/acic" plain1
mkdir -p "$OUT/traceA" "$OUT/traceB"
launch "$ROOT/bin/acic_prj" traced1 +traceoff +traceroot "$OUT/traceA"
launch "$ROOT/bin/acic" plain2
launch "$ROOT/bin/acic_prj" traced2 +traceoff +traceroot "$OUT/traceB"
launch "$ROOT/bin/acic" plain3
du -sh "$OUT/traceA" "$OUT/traceB"

python3 "$APP/benchmarks/projections_report.py" "$OUT/traceA/acic_prj" \
    --pes-per-process $((WORKERS / RPN)) --processes-per-node "$RPN" \
    --jobs 64 --json "$OUT/traceA-report.json" > "$OUT/traceA-report.md"
python3 "$APP/benchmarks/projections_report.py" "$OUT/traceB/acic_prj" \
    --pes-per-process $((WORKERS / RPN)) --processes-per-node "$RPN" \
    --jobs 64 --json "$OUT/traceB-report.json" > "$OUT/traceB-report.md"
echo "reports in $OUT"
