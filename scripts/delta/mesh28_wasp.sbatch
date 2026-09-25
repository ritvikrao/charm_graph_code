#!/usr/bin/env bash
# One interactive node only; preparation and measurement use separate allocations.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-mesh28-wasp
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign root}
MODE=${2:?prepare or run}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND
export OMP_PLACES=cores OMP_PROC_BIND=close OPENBLAS_NUM_THREADS=1
mkdir -p "$ROOT/logs" "$ROOT/graphs"
if [ "$MODE" = prepare ]; then
  base=$ROOT/graphs/mesh28-z
  if [ ! -s "$base.wsg" ]; then
    srun -N1 -n1 -c128 --cpu-bind=none "$ROOT/bin/prepare_graph" meshz 2 268435456 1 "$base.wsg.tmp" > "$base.meta.tmp"
    mv "$base.wsg.tmp" "$base.wsg"
    mv "$base.meta.tmp" "$base.meta"
  fi
  if [ ! -s "$base.reference.txt" ]; then
    OMP_NUM_THREADS=6 srun -N1 -n1 -c128 --cpu-bind=none "$ROOT/bin/reference_gap" "$base.wsg" 6 > "$base.reference.tmp"
    mv "$base.reference.tmp" "$base.reference.txt"
  fi
  sha256sum "$base.wsg" "$base.meta" "$base.reference.txt" > "$ROOT/graphs/sha256.txt"
  cat "$base.meta" "$base.reference.txt"
  echo "PREPARED $(date -Is)"
elif [ "$MODE" = run ]; then
  python3 "$APP/benchmarks/mesh28_wasp.py" "$ROOT"
else
  echo "Unknown mode $MODE" >&2
  exit 2
fi
