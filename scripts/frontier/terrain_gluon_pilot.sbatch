#!/usr/bin/env bash
# Gluon's delta on terrain-ae-z, before the held-out runs: a full run.py search
# does not fit a two-hour job when one solve takes minutes. Patched Gluon
# (gluon_sssp64, oec, 8 ranks per node, Async) solves one tuning source at each
# delta in DELTAS; every answer is checked against the certified reference row.
# Flat ground costs about 661 (cardinal) to 935 (diagonal) deciseconds per
# edge, so 1024, 8192 and 65536 advance about 1, 10 and 80 edges per round.
#
#   sbatch -N 64 scripts/frontier/terrain_gluon_pilot.sbatch CAMPAIGN
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=64
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-terrain-gpilot
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign}
NAME=${2:-terrain-ae-z}
DELTAS=${DELTAS:-"8192 1024 65536"}
LIMIT=${LIMIT:-35}
G=$ROOT/graphs/$NAME
N=$SLURM_NNODES
SOURCE=$(awk -F'\t' '$2 == "tune" {print $1}' "$G.reference.txt" | sed -n 2p)
EXPECT=$(awk -F'\t' -v s="$SOURCE" '$1 == s {print "h1=" $3 " h2=" $4 " reachable=" $5 " distance_sum=" $6}' "$G.reference.txt")
echo "job=$SLURM_JOB_ID nodes=$N source=$SOURCE expected: $EXPECT"
sha256sum "$ROOT/bin/gluon_sssp64"
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
export MPICH_OFI_NIC_POLICY=ROUND-ROBIN
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
for delta in $DELTAS; do
  out=$ROOT/logs/terrain-gluon-pilot-$SLURM_JOB_ID-d$delta.out
  /usr/bin/time -v srun -N "$N" -n $((N * 8)) --ntasks-per-node 8 -c 7 --cpu-bind=cores --unbuffered --kill-on-bad-exit=1 \
    --time="$LIMIT" "$ROOT/bin/gluon_sssp64" "$G.gr" -symmetricGraph -exec=Async -startNode="$SOURCE" -t=6 -runs=1 \
    -maxIterations=2147483647 -delta="$delta" -partition=oec > "$out" 2>&1
  status=$?
  bench=$(grep -m1 '^BENCH' "$out")
  got=$(grep -o 'h1=[0-9]* h2=[0-9]* reachable=[0-9]* distance_sum=[0-9]*' <<< "$bench")
  verdict=$([ -n "$got" ] && [ "$got" = "$EXPECT" ] && echo VALID || echo INVALID)
  echo "delta=$delta exit=$status $verdict $(grep -o 'solve_seconds=[0-9.]*' <<< "$bench") wall=$(grep -o 'Elapsed (wall clock) time.*' "$out" | sed 's/.*: //') $(grep -m1 -o 'Graph construction complete' "$out")"
done
echo "PILOT COMPLETE $SLURM_JOB_ID"
