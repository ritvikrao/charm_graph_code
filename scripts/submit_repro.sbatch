#!/usr/bin/env bash
#SBATCH --job-name=acic-repro
#SBATCH --partition=cpu
#SBATCH --account=rfp-delta-cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=16
#SBATCH --time=00:40:00
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"
exec ./scripts/repro_deadlock.sh
