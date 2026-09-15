#!/usr/bin/env bash
# Anvil wrapper for scripts/repro_stall.sh. Anvil CPU nodes are the same shape
# as Delta's -- two EPYC 7763, 128 cores, eight 16-core NUMA domains -- so the
# 8 x 15 layout launch_acic.sh builds is one process per NUMA domain with one
# core left for the OS, exactly as on Delta. What differs is the launcher:
# Slurm here has no default MPI plugin and LCT is built with PMI2 but not PMIx,
# and the runtime libraries come from modules rather than the Cray PE.
#
#   ACIC_BENCH_ROOT=/anvil/scratch/$USER/acic/campaign GRAPH=mesh22 \
#     sbatch --export=ALL scripts/anvil/repro_skew.sbatch
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=16
#SBATCH --exclusive
#SBATCH --time=00:40:00
#SBATCH --job-name=acic-stall
set -euo pipefail
ulimit -c 0
module load gcc/11.2.0 openmpi/4.0.6 libfabric/1.12.0 numactl/2.0.14 hwloc/1.11.13 >/dev/null 2>&1
export SLURM_MPI_TYPE=pmi2
cd "${SLURM_SUBMIT_DIR:-$PWD}"
exec ./scripts/repro_stall.sh
