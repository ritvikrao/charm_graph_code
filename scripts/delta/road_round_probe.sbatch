#!/usr/bin/env bash
# Override -N for the matched multi-node probe; all launches use +old-scheduler.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=00:20:00
#SBATCH --job-name=acic-road-rounds
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign directory}
shift
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND
python3 "$APP/benchmarks/road_round_probe.py" "$ROOT" "$@"
