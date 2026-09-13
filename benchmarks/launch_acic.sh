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
# One core per process is left outside the worker region, for the OS. It used
# to be called a communication core and named with +commap, but Reconverse has
# no communication thread -- see reconverse/src/cpuaffinity.cpp, "also no
# commap, we have no commthreads" -- so the flag was never parsed and no ACIC
# thread was ever placed there. The core stays free; only the inert flag is
# gone, so these layouts remain exactly the ones that were measured.
if [ $((WORKERS % RANKS)) -ne 0 ] || [ "$PER_RANK" -ge "$STRIDE" ]; then
  echo 'Layout must leave one core per process outside the worker region' >&2
  exit 2
fi
exec "$@" +ppn "$PER_RANK" +pemap "$FIRST-$LAST" +lci_ndevices 4
