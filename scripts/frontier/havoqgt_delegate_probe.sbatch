#!/usr/bin/env bash
# O2b: does HavoqGT's SSSP hang whenever the graph has delegates? At 16 nodes,
# rmat25/rmat26 ingested with delegate threshold 896 (245,506 delegates) never
# returned from any solve (5561484), while the upstream threshold (2^20: no
# delegates on rmat25) solved in 1.6-2.1 s (5561483).
#
# Here a small RMAT (rmat20 by default) is ingested at each threshold in
# THRESHOLDS on NODES nodes x 56 ranks and solved from one held-out source with
# a short step limit. A solve still running after WAIT seconds gets gdb stacks
# of three ranks per node, then is killed. The ingest's delegate count is
# printed for each threshold.
#
#   sbatch scripts/frontier/havoqgt_delegate_probe.sbatch CAMPAIGN [GRAPH]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --qos=debug
#SBATCH --nodes=2
#SBATCH --time=00:45:00
#SBATCH --job-name=acic-havoqgt-delegates
set -uo pipefail
ulimit -c 0
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
export MPICH_OFI_NIC_POLICY=ROUND-ROBIN
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
CAMPAIGN=${1:?campaign}
GRAPH=${2:-rmat20}
THRESHOLDS=${THRESHOLDS:-1048576 65536 4096 896}
WAIT=${WAIT:-120}
NN=$SLURM_NNODES
BIN=$CAMPAIGN/bin
WSG=$CAMPAIGN/graphs/$GRAPH.wsg
LOG=$CAMPAIGN/logs/havoqgt-delegates-$SLURM_JOB_ID
SHM=/dev/shm/$USER-havoqgt-$SLURM_JOB_ID
mkdir -p "$LOG"
echo "job=$SLURM_JOB_ID hosts=$SLURM_JOB_NODELIST graph=$GRAPH thresholds=$THRESHOLDS wait=$WAIT"
sha256sum "$BIN"/havoqgt_{ingest,sssp}
SRC=$(awk '$2 == "test" { print $1; exit }' "$CAMPAIGN/graphs/$GRAPH.reference.txt")
WANT=$(awk -v s="$SRC" '$1 == s { printf "h1=%s h2=%s reachable=%s distance_sum=%s", $3, $4, $5, $6; exit }' "$CAMPAIGN/graphs/$GRAPH.reference.txt")
clear_shm() { srun -N "$NN" --ntasks-per-node=1 --overlap rm -rf "$SHM" >/dev/null 2>&1; }
trap clear_shm EXIT

for d in $THRESHOLDS; do
  clear_shm
  srun -N "$NN" -n $((NN * 56)) --ntasks-per-node 56 -c 1 --cpu-bind=cores --unbuffered --time=10 \
    "$BIN/havoqgt_ingest" -O -d "$d" -o "$SHM/g" "$WSG" > "$LOG/ingest-d$d.out" 2>&1
  echo "== threshold $d: ingest rc=$? $(grep -m1 'Number of Delegates' "$LOG/ingest-d$d.out" | tr -s ' \t' ' ')"
  out=$LOG/sssp-d$d.out start=$SECONDS
  srun -N "$NN" -n $((NN * 56)) --ntasks-per-node 56 -c 1 --cpu-bind=cores --unbuffered --time=5 \
    "$BIN/havoqgt_sssp" -i "$SHM/g" -s "$SRC" > "$out" 2>&1 &
  pid=$!
  while kill -0 $pid 2>/dev/null && [ $((SECONDS - start)) -lt "$WAIT" ]; do sleep 2; done
  if kill -0 $pid 2>/dev/null; then
    echo "   HANG after ${WAIT}s: stacks in $LOG/sssp-d$d.stacks"
    srun -N "$NN" --ntasks-per-node=1 --overlap bash -c '
      for p in $(pgrep -u "$USER" -f havoqgt_sssp | head -3); do
        echo "== $(hostname) pid $p"; gdb -p "$p" -batch -ex "thread apply all bt 30" 2>/dev/null | grep -E "^#|^Thread" | head -60
      done' > "$LOG/sssp-d$d.stacks" 2>&1
    kill -TERM $pid; sleep 5; kill -KILL $pid 2>/dev/null; wait $pid
    grep -E '^#' "$LOG/sssp-d$d.stacks" | awk '{$1=""; $2=""; print}' | sort | uniq -c | sort -rn | head -12 | sed 's/^/   | /'
    continue
  fi
  wait $pid; rc=$?
  got=$(grep -m1 "^BENCH source=$SRC " "$out" | grep -oE '(h1|h2|reachable|distance_sum)=[0-9]+' | paste -sd' ')
  t=$(grep -m1 "^BENCH source=$SRC " "$out" | grep -oE 'solve_seconds=[0-9.]+')
  [ "$got" = "$WANT" ] && echo "   PASS rc=$rc $t" || { echo "   FAIL rc=$rc got=[$got] want=[$WANT]"; tail -5 "$out" | sed 's/^/   | /'; }
done
echo "HAVOQGT DELEGATE PROBE COMPLETE"
