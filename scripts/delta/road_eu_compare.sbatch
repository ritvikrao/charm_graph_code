#!/usr/bin/env bash
# One-node road-eu ACIC/Wasp comparison; graph and all outputs live on HDD.
# sbatch --output=/work/hdd/mzu/rao1/acic-road-eu-20260925/compare-%j.out \
#   scripts/delta/road_eu_compare.sbatch /work/hdd/mzu/rao1/acic-road-eu-20260925
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-road-eu
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign directory}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_PROF_TIMER ACIC_PROF_OFF ACIC_PROF_DIR ACIC_PREBIND ACIC_LAUNCH_PREFIX
python3 "$APP/benchmarks/road_eu_compare.py" "$ROOT"
