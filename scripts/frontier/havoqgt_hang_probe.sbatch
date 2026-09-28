#!/usr/bin/env bash
# Why does havoqgt_ingest hang on two nodes (smoke job 5558543: road-ny and
# mesh20 at 2 x 56 ranks printed nothing and were killed at the 20-minute step
# limit, while 1 x 56 finished in under a second)? Each case gets a short step
# limit. When a case is still running after WAIT seconds, gdb takes the stacks
# of three ranks on each node, then the step is cancelled.
#
# Cases: 2 x 56 into /dev/shm (the hang), 2 x 8 into /dev/shm (fewer ranks),
# 2 x 56 into a Lustre directory (shared store path), then havoqgt_sssp on
# whichever store was built.
#
#   sbatch scripts/frontier/havoqgt_hang_probe.sbatch CAMPAIGN [GRAPH]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --qos=debug
#SBATCH --nodes=2
#SBATCH --time=00:40:00
#SBATCH --job-name=acic-havoqgt-probe
set -uo pipefail
ulimit -c 0
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
CAMPAIGN=${1:?campaign}
GRAPH=${2:-road-ny}
WAIT=${WAIT:-90}
BIN=$CAMPAIGN/bin
WSG=$CAMPAIGN/graphs/$GRAPH.wsg
LOG=$CAMPAIGN/logs/havoqgt-probe-$SLURM_JOB_ID
SHM=/dev/shm/$USER-havoqgt-$SLURM_JOB_ID
LUS=$CAMPAIGN/graphs-baseline-tmp/havoqgt-probe-$SLURM_JOB_ID
mkdir -p "$LOG" "$LUS"
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid NO_AFFINITY=1 MPICH_OFI_NIC_POLICY=ROUND-ROBIN
echo "job=$SLURM_JOB_ID hosts=$SLURM_JOB_NODELIST graph=$GRAPH wait=$WAIT"
SRC=$(awk '$2 == "test" { print $1; exit }' "$CAMPAIGN/graphs/$GRAPH.reference.txt")
WANT=$(awk -v s="$SRC" '$1 == s { printf "h1=%s h2=%s reachable=%s distance_sum=%s", $3, $4, $5, $6; exit }' "$CAMPAIGN/graphs/$GRAPH.reference.txt")
trap 'srun -N 2 --ntasks-per-node=1 --overlap rm -rf "$SHM" >/dev/null 2>&1; rm -rf "$LUS"' EXIT

stacks() {  # label binary-name
  srun -N 2 --ntasks-per-node=1 --overlap bash -c '
    for p in $(pgrep -u "$USER" -f "'"$2"'" | head -3); do
      echo "== $(hostname) pid $p"; gdb -p "$p" -batch -ex "thread apply all bt 25" 2>/dev/null | grep -E "^#|^Thread" | head -60
    done' > "$LOG/$1.stacks" 2>&1
}
run_case() {  # label rpn binary args...   (NODES=1 for a one-node step)
  local label=$1 rpn=$2 name=$3; shift 3
  local out=$LOG/$label.out start=$SECONDS nodes=${NODES:-2}
  srun -N $nodes -n $((nodes * rpn)) --ntasks-per-node "$rpn" -c $((56 / rpn)) --cpu-bind=cores --unbuffered \
    --time=5 "$BIN/$name" "$@" > "$out" 2>&1 &
  local pid=$!
  while kill -0 $pid 2>/dev/null && [ $((SECONDS - start)) -lt "$WAIT" ]; do sleep 2; done
  if kill -0 $pid 2>/dev/null; then
    echo "HANG $label after ${WAIT}s: taking stacks"
    stacks "$label" "$name"
    kill -TERM $pid; sleep 5; kill -KILL $pid 2>/dev/null
    wait $pid; echo "  rc=$? (killed)"
    sed 's/^/  | /' "$LOG/$label.stacks" | head -80
    return 1
  fi
  wait $pid; local rc=$?
  echo "DONE $label rc=$rc in $((SECONDS - start))s"; tail -4 "$out" | sed 's/^/  | /'
  return $rc
}
check() {  # label
  local got
  got=$(grep -m1 "^BENCH source=$SRC " "$LOG/$1.out" | grep -oE '(h1|h2|reachable|distance_sum)=[0-9]+' | paste -sd' ')
  [ "$got" = "$WANT" ] && echo "PASS $1 $(grep -m1 "^BENCH source=$SRC " "$LOG/$1.out" | grep -oE 'solve_seconds=[0-9.]+')" || echo "FAIL $1 got=[$got] want=[$WANT]"
}

# LOOPS=K replays the smoke job's order K times before the other cases: a
# one-node ingest and solve inside the two-node allocation, then two nodes.
for k in $(seq 1 "${LOOPS:-0}"); do
  NODES=1 run_case loop$k-ingest-1x56 56 havoqgt_ingest -O -o "$SHM/g1" "$WSG" &&
    NODES=1 run_case loop$k-sssp-1x56 56 havoqgt_sssp -i "$SHM/g1" -s "$SRC" && check loop$k-sssp-1x56
  srun -N 2 --ntasks-per-node=1 --overlap rm -rf "$SHM"
  run_case loop$k-ingest-2x56 56 havoqgt_ingest -O -o "$SHM/g2" "$WSG" &&
    run_case loop$k-sssp-2x56 56 havoqgt_sssp -i "$SHM/g2" -s "$SRC" && check loop$k-sssp-2x56
  srun -N 2 --ntasks-per-node=1 --overlap rm -rf "$SHM"
done
run_case ingest-2x56-shm 56 havoqgt_ingest -O -o "$SHM/g" "$WSG" &&
  run_case sssp-2x56-shm 56 havoqgt_sssp -i "$SHM/g" -s "$SRC" && check sssp-2x56-shm
srun -N 2 --ntasks-per-node=1 --overlap rm -rf "$SHM"
run_case ingest-2x8-shm 8 havoqgt_ingest -O -o "$SHM/g" "$WSG" &&
  run_case sssp-2x8-shm 8 havoqgt_sssp -i "$SHM/g" -s "$SRC" && check sssp-2x8-shm
srun -N 2 --ntasks-per-node=1 --overlap rm -rf "$SHM"
run_case ingest-2x56-lustre 56 havoqgt_ingest -O -o "$LUS/g" "$WSG" &&
  run_case sssp-2x56-lustre 56 havoqgt_sssp -i "$LUS/g" -s "$SRC" && check sssp-2x56-lustre
echo "HAVOQGT PROBE COMPLETE"
