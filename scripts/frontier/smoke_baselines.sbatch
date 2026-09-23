#!/usr/bin/env bash
# Frontier: does every baseline binary launch here and return the reference
# answer? One tuning source per small graph; GAPBS on one node, RIKEN and
# Gluon on one and two nodes (Gluon's two-node shutdown crashed on Delta).
# Each run's BENCH digest must equal the graph's reference row -- the same
# check run.py applies. Wasp and ds-rho (Wasp SC25 artifact) have no digest
# adapter yet, so they only run with their own serial verifier (-v).
#
# Frontier nodes expose 56 cores (the first core of each L3 is reserved), so
# layouts are 8 ranks x 7 cores, not Delta's 8 x 16.
#
#   sbatch scripts/frontier/smoke_baselines.sbatch CAMPAIGN [graphs]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=2
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-smoke
set -uo pipefail
ulimit -c 0
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
CAMPAIGN=${1:?campaign directory}
GRAPHS=${2:-road-ny,rmat20,mesh20,youtube}
DEPS=${ACIC_BENCH_DEPS:-$(dirname "$CAMPAIGN")/deps}
BIN=$CAMPAIGN/bin
DATA=$CAMPAIGN/graphs
WASP=$DEPS/wasp-ae/wasp-ae/impl
export OMP_PLACES=cores OMP_PROC_BIND=close NO_AFFINITY=1
export MPICH_OFI_NIC_POLICY=ROUND-ROBIN
fail=0

check() {  # label graph source command...
  local label=$1 graph=$2 source=$3; shift 3
  local want got out
  want=$(awk -v s="$source" '$1 == s { printf "h1=%s h2=%s reachable=%s distance_sum=%s", $3, $4, $5, $6 }' "$DATA/$graph.reference.txt")
  out=$("$@" 2>&1); local rc=$?
  got=$(grep -m1 '^BENCH' <<< "$out" | grep -oE '(h1|h2|reachable|distance_sum)=[0-9]+' | paste -sd' ')
  if [ $rc -eq 0 ] && [ "$got" = "$want" ]; then
    echo "OK   $label $graph $source $(grep -m1 '^BENCH' <<< "$out" | grep -oE 'solve_seconds=[0-9.]+')"
  else
    echo "FAIL $label $graph $source rc=$rc"; echo " want: $want"; echo " got:  $got"
    tail -15 <<< "$out" | sed 's/^/  | /'; fail=1
  fi
}

ldd "$BIN/riken_sssp" "$BIN/gluon_sssp" | grep -E 'mpi|fabric|boost|sci'
for graph in ${GRAPHS//,/ }; do
  source=$(awk '$2 == "tune" { print $1; exit }' "$DATA/$graph.reference.txt")
  denominator=$(grep -oE 'riken_denominator=[0-9]+' "$DATA/$graph.meta" | cut -d= -f2)
  delta=$((denominator / 16))
  OMP_NUM_THREADS=56 check gap-1x56 "$graph" "$source" \
    srun -N 1 -n 1 -c 56 --cpu-bind=cores "$BIN/gap_sssp" "$DATA/$graph.wsg" "$source" "$delta"
  OMP_NUM_THREADS=56 check gap_work_cost-1x56 "$graph" "$source" \
    srun -N 1 -n 1 -c 56 --cpu-bind=cores "$BIN/gap_work_cost" "$DATA/$graph.wsg" "$source" "$delta"
  for nodes in 1 2; do
    OMP_NUM_THREADS=7 check "riken-${nodes}Nx8x7" "$graph" "$source" \
      srun -N "$nodes" -n $((nodes * 8)) --ntasks-per-node 8 -c 7 --cpu-bind=cores \
      "$BIN/riken_sssp" "$DATA/$graph.wsg" "$source" "$delta" "$denominator" 0
    partition=oec; [ "$nodes" = 2 ] && partition=cvc
    check "gluon-async-${nodes}Nx1x55-$partition" "$graph" "$source" \
      srun -N "$nodes" -n "$nodes" --ntasks-per-node 1 -c 56 --cpu-bind=cores \
      "$BIN/gluon_sssp" "$DATA/$graph.gr" -symmetricGraph -exec=Async -startNode="$source" \
      -t=55 -runs=1 -maxIterations=2147483647 -delta=0 -partition="$partition"
  done
  echo "--- wasp $graph (own verifier)"
  OMP_NUM_THREADS=56 srun -N 1 -n 1 -c 56 --cpu-bind=cores "$WASP/wasp/build/sssp" \
    -f "$DATA/$graph.wsg" -r "$source" -n 1 -d "$delta" -v 2>&1 | grep -iE 'verif|trial time|average' || fail=1
  echo "--- ds-rho $graph (own verifier, random source)"
  PARLAY_NUM_THREADS=56 srun -N 1 -n 1 -c 56 --cpu-bind=cores "$WASP/ds-rho/sssp" \
    -i "$DATA/$graph.wsg" -w -s -v -n 1 -S 1 2>&1 | grep -iE 'pass|fail|error|verif|average' || fail=1
done
[ $fail = 0 ] && echo "SMOKE PASSED" || echo "SMOKE FAILED"
