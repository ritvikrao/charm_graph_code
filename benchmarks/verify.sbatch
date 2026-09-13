#!/usr/bin/env bash
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=00:20:00
#SBATCH --job-name=acic-regression
set -euo pipefail
ulimit -c 0
cd /u/rao1/charm_graph_code
export PMI_MAX_KVS_ENTRIES=1000
export FI_CXI_RX_MATCH_MODE=hybrid
bash scripts/verify.sh
