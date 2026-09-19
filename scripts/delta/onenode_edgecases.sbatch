#!/usr/bin/env bash
# Small/empty partitions and source reset across disconnected components.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=00:15:00
#SBATCH --job-name=acic-gap-edgecases
set -euo pipefail
ROOT=${1:?campaign}
LABEL=${2:-acic_checked}
if [ "$#" -ge 2 ]; then shift 2; else shift; fi
EXTRA=("$@")
OUT=$ROOT/logs/edgecases-$SLURM_JOB_ID
mkdir -p "$OUT"
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid SLURM_MPI_TYPE=cray_shasta
unset LCT_PMI_BACKEND
python3 - "$OUT" <<'PY'
import struct, sys
from pathlib import Path
root = Path(sys.argv[1])
for n in [16, 256, 1023, 1024, 1025]:
    for shape in ['empty', 'path']:
        adjacency = [[] for _ in range(n)]
        if shape == 'path':
            for v in range(n-2):
                weight = 1 + v % 17
                adjacency[v].append((v+1, weight))
                adjacency[v+1].append((v, weight))
        offsets = [0]
        for row in adjacency: offsets.append(offsets[-1] + len(row))
        with (root / f'{shape}-{n}.wsg').open('wb') as f:
            f.write(struct.pack('=Bqq', 0, offsets[-1], n))
            f.write(struct.pack('=' + 'q' * len(offsets), *offsets))
            for row in adjacency:
                for v, w in row: f.write(struct.pack('=ii', v, w))
PY
for graph in "$OUT/"*.wsg; do
  n=${graph##*-}; n=${n%.wsg}
  for share in off on; do
    log=${graph%.wsg}-$share.out
    srun -N "$SLURM_NNODES" -n "$SLURM_NNODES" --ntasks-per-node=1 -c 16 \
      --cpu-bind=none --unbuffered --kill-on-bad-exit=1 \
      "$ROOT/bin/$LABEL" 0 "$graph" 1 0 4 0.999 0.005 \
      --sources "0,$((n - 1)),0" --verify --timeout 30 \
      --process-share "$share" --slack-control on "${EXTRA[@]}" +ppn 4 +lci_ndevices 4 > "$log" 2>&1
    [ "$(grep -c '^VERIFY PASS' "$log")" = 3 ]
    if grep -qE '^STALL_RESCUE|^PROGRESS_STALL|^CONSERVATION VIOLATED' "$log"; then exit 1; fi
  done
done
echo "EDGE CASES PASS $OUT"
