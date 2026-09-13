#!/usr/bin/env bash
# Bind each Charm++ process to a disjoint group of Delta cores.
set -euo pipefail
RANKS=${1:?ranks per physical node}
WORKERS=${2:?workers per physical node}
shift 2
LOCAL_RANK=${SLURM_LOCALID:-0}
PER_RANK=$((WORKERS / RANKS))
STRIDE=$((128 / RANKS))
FIRST=$((LOCAL_RANK * STRIDE))
LAST=$((FIRST + PER_RANK - 1))
COMM=$((LAST + 1))
if [ $((WORKERS % RANKS)) -ne 0 ] || [ "$PER_RANK" -ge "$STRIDE" ]; then
  echo 'Layout must leave a communication core per process' >&2
  exit 2
fi
exec "$@" +ppn "$PER_RANK" +pemap "$FIRST-$LAST" +commap "$COMM" +lci_ndevices 4
