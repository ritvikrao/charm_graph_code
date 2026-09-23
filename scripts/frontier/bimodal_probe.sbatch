#!/usr/bin/env bash
# Probe for the launch-level bimodality in the RMAT tail (ipdps27-frontier.md
# §6): one binary and flag set under different network settings, arms
# rotated, then benchmarks/tail_split.py per graph. A diagnosis, not a gate,
# so there is no priority-run audit; every solve is still digest-checked.
#
#   sbatch scripts/frontier/bimodal_probe.sbatch CAMPAIGN VARIANTS.json [graphs] [reps]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=8
#SBATCH --time=00:45:00
#SBATCH --job-name=acic-bimodal
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
GRAPHS=${3:-rmat27,uniform25}
REPS=${4:-6}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
# The kernel's huge-page policy, for the launch-state probes.
srun -N "$SLURM_NNODES" --ntasks-per-node=1 bash -c 'echo "$(hostname) THP $(cat /sys/kernel/mm/transparent_hugepage/enabled) defrag $(cat /sys/kernel/mm/transparent_hugepage/defrag)"' | sort | uniq -c -f1
srun -N 1 --ntasks=1 bash -c 'numactl -H | head -3; lscpu | grep -E "NUMA node[0-9]"'
for graph in ${GRAPHS//,/ }; do
  "$PY" "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$VARIANTS" \
    --sources 4 --reps "$REPS" --source-role test --batch > /dev/null
  echo "### $graph"
  "$PY" "$APP/benchmarks/tail_split.py" "$ROOT/logs/AB-$graph-${SLURM_NNODES}n-$SLURM_JOB_ID"
done
echo "PROBE COMPLETE $SLURM_JOB_ID"
