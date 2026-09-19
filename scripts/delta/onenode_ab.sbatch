#!/usr/bin/env bash
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-gap-AB
set -euo pipefail
ulimit -c 0
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
python3 "$APP/benchmarks/onenode_ab.py" "$@"
