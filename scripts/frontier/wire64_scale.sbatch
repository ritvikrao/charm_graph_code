#!/usr/bin/env bash
# A graph past 2^32 vertices: ACIC's in-memory 2-D mesh (mode 2) with
# V = VERTICES (default 2^32 + 2^30, a 73271 x 73271 grid plus isolated
# vertices), sources above 2^32 and near 0. The WIRE=compact64 build runs at
# NODES_LIST node counts in one allocation; each solve must pass --certify, and
# the digests must agree across node counts. Then the compact (31-bit) build
# must refuse the graph with its wire error rather than answer. (No
# --reader-tile: it applies only to files.)
#
#   sbatch -N 16 scripts/frontier/wire64_scale.sbatch CAMPAIGN
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=16
#SBATCH --time=00:45:00
#SBATCH --job-name=acic-w64scale
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
VERTICES=${VERTICES:-5368709120}
SOURCES=${SOURCES:-5000000000,17}
NODES_LIST=${NODES_LIST:-"8 16"}
RPN=8
WORKERS=56
FLAGS="--process-share auto --slack-control off --process-queue nearest --process-queue-batch 8 +old-scheduler --heap-slice 8"
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
OUT=$ROOT/logs/wire64-scale-$SLURM_JOB_ID
mkdir -p "$OUT"
echo "job=$SLURM_JOB_ID hosts=$SLURM_JOB_NODELIST vertices=$VERTICES sources=$SOURCES"
sha256sum "$ROOT/bin/acic_w32" "$ROOT/bin/acic_w64"
run() {  # run LABEL BINARY NODES [flags...]
  local label=$1 bin=$2 nodes=$3; shift 3
  /usr/bin/time -v srun -N "$nodes" -n $((nodes * RPN)) --ntasks-per-node "$RPN" -c $((WORKERS / RPN)) --cpu-bind=none \
    --unbuffered --kill-on-bad-exit=1 --time=00:15:00 \
    bash "$APP/benchmarks/launch_acic.sh" "$RPN" "$WORKERS" "$ROOT/bin/$bin" "$VERTICES" 4 1 "${SOURCES%%,*}" 2 0.999 0.005 \
    --result-digest --timeout 600 $FLAGS --sources "$SOURCES" "$@" > "$OUT/$label.out" 2>&1
  local status=$?
  echo "$label: exit $status, wall $(grep -o 'Elapsed (wall clock) time.*' "$OUT/$label.out" | sed 's/.*: //')"
  grep -E '^(SOURCE_RUN|Compute time|Setup time|VERIFY parallel|CERTIFY|Graph bytes)' "$OUT/$label.out" | cut -c1-200
  return $status
}
FAIL=0
for n in $NODES_LIST; do
  run "w64-${n}n" acic_w64 "$n" --certify || FAIL=1
  [ "$(grep -c '^CERTIFY PASS' "$OUT/w64-${n}n.out")" = "$(tr ',' '\n' <<< "$SOURCES" | wc -l)" ] || FAIL=1
done
first=$(set -- $NODES_LIST; echo "$1")
for n in $NODES_LIST; do
  if [ "$(grep -o 'VERIFY parallel digest.*' "$OUT/w64-${n}n.out")" != "$(grep -o 'VERIFY parallel digest.*' "$OUT/w64-${first}n.out")" ]; then
    echo "digests differ between ${first} and ${n} nodes"; FAIL=1
  fi
done
run "w32-${first}n" acic_w32 "$first"
if grep -q 'compact wire: vertex' "$OUT/w32-${first}n.out"; then
  echo "compact build refused the graph as it should"
else
  echo "compact build did not report the wire limit"; FAIL=1
fi
echo "WIRE64 SCALE $([ $FAIL = 0 ] && echo PASSED || echo FAILED) $SLURM_JOB_ID"
