#!/usr/bin/env bash
# Frontier: check that launch_acic.sh's layouts bind every PE to a usable core.
# Runs the Reconverse tree's ckhello through the same srun line as
# onenode_ab.py, with +showcpuaffinity. CmiSetCPUAffinity aborts on a core
# outside the job's cpuset (PUs 0, 8, ..., 56 are reserved), so a clean exit
# plus the printed PE -> PU list is the check.
#
#   sbatch scripts/frontier/check_affinity.sbatch CHARM_TREE
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=00:10:00
#SBATCH --job-name=acic-affinity
set -uo pipefail
TREE=${1:?Reconverse build tree, e.g. .../charm/reconverse-linux-x86_64}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export PMI_MAX_KVS_ENTRIES=100000 SLURM_MPI_TYPE=cray_shasta
fail=0
echo "job cpuset: $(srun -N1 -n1 --overlap bash -c 'grep Cpus_allowed_list /proc/self/status')"
for layout in 8x7 4x14 2x28 1x56 8x6; do
  rpn=${layout%x*}; per=${layout#*x}
  cpus=$(python3 "$APP/benchmarks/machine.py" cpus "$rpn")
  out=$(srun -N 1 -n "$rpn" --ntasks-per-node "$rpn" -c "$cpus" --network=single_node_vni \
        --cpu-bind=none --kill-on-bad-exit=1 \
        bash "$APP/benchmarks/launch_acic.sh" "$rpn" $((rpn * per)) "$TREE/bin/ckhello" +showcpuaffinity 2>&1)
  rc=$?
  pus=$(grep -oE 'to PU P#[0-9]+' <<< "$out" | grep -oE '[0-9]+$' | sort -n | paste -sd,)
  n=$(grep -c 'to PU P#' <<< "$out")
  bad=$(tr , '\n' <<< "$pus" | awk '$1 % 8 == 0' | paste -sd,)
  dup=$(tr , '\n' <<< "$pus" | sort | uniq -d | paste -sd,)
  if [ $rc -eq 0 ] && [ "$n" -eq $((rpn * per)) ] && [ -z "$bad" ] && [ -z "$dup" ]; then
    echo "OK   $layout: $n PEs on PUs $pus"
  else
    echo "FAIL $layout rc=$rc PEs=$n reserved=[$bad] duplicate=[$dup]"; tail -20 <<< "$out" | sed 's/^/  | /'; fail=1
  fi
done
[ $fail = 0 ] && echo "AFFINITY PASSED" || echo "AFFINITY FAILED"
