#!/usr/bin/env bash
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-compare
set -euo pipefail
ulimit -c 0
APP=${ACIC_BENCH_APP:-/u/rao1/charm_graph_code}
printf 'job=%s hosts=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST"
sha256sum "$1/bin/acic" "$1/bin/riken_sssp" "$1/bin/gap_sssp"
lscpu
python3 -u "$APP/benchmarks/run.py" "$@"
