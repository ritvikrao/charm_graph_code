#!/usr/bin/env bash
# Bind each Charm++ process to a disjoint group of cores.
#
# The layout comes from benchmarks/machine.py. On Delta and Anvil a process
# gets a 128/RANKS-core stride and leaves one core outside its worker region,
# for the OS; that is the 8 x 15 layout measured since step 7.5. On Frontier
# the OS core is already reserved by Slurm (PUs 0, 8, ..., 56), so a process
# owns 56/RANKS usable cores and may run a worker on each: 8 x 7 there is the
# same shape as Delta's 8 x 15.
#
# That free core used to be called a communication core and named with
# +commap, but Reconverse has no communication thread -- see
# reconverse/src/cpuaffinity.cpp, "also no commap, we have no commthreads" --
# so the flag was never parsed and no ACIC thread was ever placed there.
set -euo pipefail
RANKS=${1:?ranks per physical node}
WORKERS=${2:?workers per physical node}
shift 2
LOCAL_RANK=${SLURM_LOCALID:-0}
if [ $((WORKERS % RANKS)) -ne 0 ]; then
  echo "$WORKERS workers do not split evenly over $RANKS processes" >&2
  exit 2
fi
PER_RANK=$((WORKERS / RANKS))
MACHINE_PY=$(dirname "$0")/machine.py
LIMIT=$(( $(python3 "$MACHINE_PY" workers "$RANKS") / RANKS ))
if [ "$PER_RANK" -gt "$LIMIT" ]; then
  echo "Layout must leave the OS core free: at most $LIMIT workers per process" >&2
  exit 2
fi
MAP=$(python3 "$MACHINE_PY" pemap "$LOCAL_RANK" "$RANKS" "$PER_RANK")
# ACIC_STDERR_DIR sends each rank's stderr to its own file, so network probes
# can log libfabric warnings without breaking the per-source result markers.
if [ -n "${ACIC_STDERR_DIR:-}" ]; then
  mkdir -p "$ACIC_STDERR_DIR"
  [ "${SLURM_PROCID:-0}" = 0 ] && echo "PROBE_STEP ${SLURM_JOB_ID:-0}.${SLURM_STEP_ID:-0}"
  exec 2>"$ACIC_STDERR_DIR/${SLURM_JOB_ID:-0}.${SLURM_STEP_ID:-0}.rank${SLURM_PROCID:-0}.err"
fi
# Launch-state probes: ACIC_LAUNCH_PREFIX runs the solver under a wrapper
# (setarch -R, nothp), and ACIC_MEMLOG_DIR samples each rank's resident and
# huge-page memory every two seconds until it exits. Both are off by default.
if [ -n "${ACIC_MEMLOG_DIR:-}" ]; then
  mkdir -p "$ACIC_MEMLOG_DIR"
  [ "${SLURM_PROCID:-0}" = 0 ] && echo "PROBE_STEP ${SLURM_JOB_ID:-0}.${SLURM_STEP_ID:-0}"
  ( pid=$$
    while kill -0 "$pid" 2>/dev/null; do
      awk '/^(Rss|AnonHugePages):/ {printf "%s %s ", $1, $2}' "/proc/$pid/smaps_rollup" 2>/dev/null
      # Pages per NUMA node, when asked: where the rank's memory actually is.
      [ -n "${ACIC_MEMLOG_NUMA:-}" ] && awk '{for (i = 1; i <= NF; i++) if ($i ~ /^N[0-9]+=/) {split($i, a, "="); n[a[1]] += a[2]}}
        END {for (k in n) printf "%s: %d ", k, n[k]}' "/proc/$pid/numa_maps" 2>/dev/null
      echo
      sleep 2
    done ) > "$ACIC_MEMLOG_DIR/${SLURM_JOB_ID:-0}.${SLURM_STEP_ID:-0}.rank${SLURM_PROCID:-0}.mem" &
fi
# ACIC_DIAG_DIR writes the controller's per-round record (--diag) for every
# source of every launch, under the launch's job.step name.
if [ -n "${ACIC_DIAG_DIR:-}" ]; then
  mkdir -p "$ACIC_DIAG_DIR"
  [ "${SLURM_PROCID:-0}" = 0 ] && [ -z "${ACIC_MEMLOG_DIR:-}${ACIC_STDERR_DIR:-}" ] &&
    echo "PROBE_STEP ${SLURM_JOB_ID:-0}.${SLURM_STEP_ID:-0}"
  set -- "$@" --diag "$ACIC_DIAG_DIR/${SLURM_JOB_ID:-0}.${SLURM_STEP_ID:-0}"
fi
# ACIC_LCI_NDEVICES exists for the network probes; every result so far used 4.
# ACIC_PREBIND=1 starts the process on its own worker cores, so memory it
# allocates before Reconverse pins its threads is local to them.
PREBIND=()
[ -n "${ACIC_PREBIND:-}" ] && PREBIND=(taskset -c "$MAP")
exec "${PREBIND[@]}" ${ACIC_LAUNCH_PREFIX:-} "$@" +ppn "$PER_RANK" +pemap "$MAP" +lci_ndevices "${ACIC_LCI_NDEVICES:-4}"
