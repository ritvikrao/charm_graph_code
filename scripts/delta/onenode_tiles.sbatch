#!/usr/bin/env bash
# Build the full L1 file sweep, one graph per job; systems share each file.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=200G
#SBATCH --time=04:00:00
#SBATCH --job-name=acic-L1-inputs
set -euo pipefail
ROOT=${1:?campaign directory}
GRAPH=${2:?Morton graph name}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
WORKERS=${WORKERS:-120}
export PYTHONNOUSERSITE=1 OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1
vertices=$(python3 - "$ROOT/graphs/$GRAPH.wsg" <<'PY'
import struct, sys
with open(sys.argv[1], 'rb') as f:
    print(struct.unpack('=Bqq', f.read(17))[2])
PY
)
for nodes in 1 2 8; do
  pes=$((nodes * WORKERS))
  for pieces in 1 4 16 64; do
    tile=$((vertices / (pieces * pes)))
    ((tile > 0)) || tile=1
    out=$GRAPH-p$pes-t$tile
    if [ ! -s "$ROOT/graphs/$out.reference.txt" ]; then
      python3 "$APP/benchmarks/reorder_graph.py" "$ROOT" "$GRAPH" "$out" ordered \
        --tile "$tile" --pes "$pes"
    fi
  done
done
