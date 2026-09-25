#!/usr/bin/env bash
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-wasp-boundary
set -euo pipefail
ulimit -c 0
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_PROF_TIMER ACIC_PROF_OFF ACIC_PROF_DIR
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
python3 "$APP/benchmarks/uniform_wasp_boundary.py" "$@"
