#!/usr/bin/env bash
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-mesh28-opt
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
CONFIG=${2:?variants json}
ROLE=${3:-tune}
SOURCES=${4:-2}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND
if [ -n "${GATE_BINARIES:-}" ]; then
  read -ra bins <<< "$GATE_BINARIES"
  python3 "$APP/benchmarks/mesh28_opt_gate.py" "$ROOT" "${bins[@]}"
fi
python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" mesh28-z "$CONFIG" --rpn 16 --workers 112 --sources "$SOURCES" --source-role "$ROLE" --reps 3
