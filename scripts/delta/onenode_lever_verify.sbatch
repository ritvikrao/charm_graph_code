#!/usr/bin/env bash
# Correctness for an isolated variant build; pass application flags after LABEL.
# One node: complete existing gate. Two nodes: transport + sharing fixtures.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-lever-verify
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
LABEL=${2:?binary label}
shift 2
BUILD=$(sed -n 's/.* build=//p' "$ROOT/bin/$LABEL.manifest")
OUT=$ROOT/logs/$LABEL-verify-$SLURM_JOB_ID
mkdir -p "$OUT"
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid SLURM_MPI_TYPE=cray_shasta
if [ "$SLURM_NNODES" = 1 ]; then
  export LCT_PMI_BACKEND=local
  export SSSP_EXTRA_ARGS="$*"
  export SSSP_MAKE_ARGS="CHARMC_SMP=$HOME/charm_reconverse/bin/charmc HTRAM_DIR=$BUILD/htram"
  (cd "$BUILD/src"; bash scripts/verify.sh) > "$OUT/gate.out" 2>&1
else
  unset LCT_PMI_BACKEND
  for mode in 1 2 3; do
    for source in 0 777; do
      log=$OUT/m$mode-s$source.out
      srun -N "$SLURM_NNODES" -n "$SLURM_NNODES" --ntasks-per-node=1 -c 16 \
        --cpu-bind=none --unbuffered --kill-on-bad-exit=1 \
        "$ROOT/bin/$LABEL" 16384 262144 1 "$source" "$mode" 0.999 0.005 \
        --verify --timeout 120 "$@" +ppn 4 +lci_ndevices 4 > "$log" 2>&1
      grep -q '^VERIFY PASS' "$log"
      if grep -qE '^STALL_RESCUE|^PROGRESS_STALL|^CONSERVATION VIOLATED' "$log"; then exit 1; fi
    done
  done
fi
echo "LEVER VERIFY PASS $LABEL $OUT"
