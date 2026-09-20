#!/usr/bin/env bash
# Structural corroboration while exclusive whole-node R0 jobs wait.
# A shared-node allocation: these times are NOT paper acceptance measurements.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=64
#SBATCH --mem=8G
#SBATCH --time=00:03:00
#SBATCH --job-name=GAP-R0-work
set -euo pipefail
ulimit -c 0
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
python3 "$APP/benchmarks/work_cost_gap.py" "${1:?campaign}" road-usa-z
