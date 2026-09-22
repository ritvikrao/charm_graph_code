#!/usr/bin/env bash
# GAPBS at fixed, training-selected settings on held-out sources
# (design/ipdps27-c6-mesh.md). Usage: sbatch gap_fixed.sbatch ROOT SETTINGS
# where SETTINGS is graph=threads:delta[,...].
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --time=00:15:00
#SBATCH --job-name=gap-fixed
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
SETTINGS=${2:?graph=threads:delta,...}
APP=${ACIC_BENCH_APP:?frozen harness copy}
source /anvil/scratch/x-rrao/acic/env.sh
GRAPHS=$(tr ',' '\n' <<<"$SETTINGS" | cut -d= -f1 | sort -u | paste -sd,)
python3 "$APP/benchmarks/gap_anvil.py" "$ROOT" --graphs "$GRAPHS" --settings "$SETTINGS" --roles test
