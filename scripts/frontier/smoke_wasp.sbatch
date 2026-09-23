#!/usr/bin/env bash
# Frontier: does the Wasp digest adapter (benchmarks/wasp_driver.cpp, built by
# build_baselines.sh as bin/wasp_sssp) return the reference answer? Every
# source of each graph, one node, 56 threads, delta = riken_denominator/16
# unless DELTA_<graph> overrides it. A matching digest also proves Wasp read
# the native .wsg weights (distance_sum depends on every weight).
#
#   sbatch scripts/frontier/smoke_wasp.sbatch CAMPAIGN [graphs]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-wasp-smoke
set -uo pipefail
ulimit -c 0
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 >/dev/null 2>&1
CAMPAIGN=${1:?campaign directory}
GRAPHS=${2:-road-ny,mesh20,rmat20,youtube,road-usa-z,mesh26-z,rmat25}
BIN=$CAMPAIGN/bin
DATA=$CAMPAIGN/graphs
export OMP_PLACES=cores OMP_PROC_BIND=close OMP_NUM_THREADS=56
sha256sum "$BIN/wasp_sssp"
fail=0
for graph in ${GRAPHS//,/ }; do
  denominator=$(grep -oE 'riken_denominator=[0-9]+' "$DATA/$graph.meta" | cut -d= -f2)
  var=DELTA_${graph//-/_}
  delta=${!var:-$((denominator / 16))}
  for source in $(awk '$1 ~ /^[0-9]+$/ { print $1 }' "$DATA/$graph.reference.txt"); do
    want=$(awk -v s="$source" '$1 == s { printf "h1=%s h2=%s reachable=%s distance_sum=%s", $3, $4, $5, $6 }' "$DATA/$graph.reference.txt")
    out=$(srun -N 1 -n 1 -c 56 --cpu-bind=cores "$BIN/wasp_sssp" "$DATA/$graph.wsg" "$source" "$delta" 2>&1); rc=$?
    got=$(grep -m1 '^BENCH' <<< "$out" | grep -oE '(h1|h2|reachable|distance_sum)=[0-9]+' | paste -sd' ')
    if [ $rc -eq 0 ] && [ "$got" = "$want" ]; then
      echo "OK   wasp $graph $source d=$delta $(grep -m1 '^BENCH' <<< "$out" | grep -oE '(solve_seconds|rss_kib)=[0-9.]+' | paste -sd' ')"
    else
      echo "FAIL wasp $graph $source d=$delta rc=$rc"; echo " want: $want"; echo " got:  $got"
      tail -15 <<< "$out" | sed 's/^/  | /'; fail=1
    fi
  done
done
[ $fail = 0 ] && echo "WASP SMOKE PASSED" || echo "WASP SMOKE FAILED"
