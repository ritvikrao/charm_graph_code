#!/usr/bin/env bash
#
# Correctness regression gate for sssp_smp.
#
# Two independent checks run for every configuration:
#
#   1. --verify compares the parallel result against serial Dijkstra over the
#      same graph, in-process. This catches algorithm and runtime bugs.
#
#   2. The resulting digest is compared against scripts/golden_digests.txt.
#      Check 1 alone cannot catch a change to the graph generator, because the
#      parallel and serial paths share it -- both would move together and still
#      agree. The golden file pins the graph itself.
#
# The generated graph is a pure function of (V, edges, seed, mode), so a digest
# must also be identical across PE counts. The matrix below varies ppn for that
# reason; every row of a given graph must produce the same digest.
#
# Usage:
#   scripts/verify.sh                  # run the gate
#   scripts/verify.sh --update-golden  # re-record the golden digests
#
# Environment:
#   SSSP_PE_FLAG   worker-thread flag for the target Charm build. Reconverse
#                  wants "+ppn" (and rejects +p alongside it); a classic
#                  multicore build wants "+p". Default "+ppn".
#   SSSP_MAKE_ARGS extra variables for make, e.g. CHARMC_SMP=... HTRAM_DIR=...
#                  These must be passed on make's command line: the Makefile
#                  assigns them with "=", which beats the environment.
#
set -uo pipefail

cd "$(dirname "$0")/.."
GOLDEN="scripts/golden_digests.txt"
PE_FLAG="${SSSP_PE_FLAG:-+ppn}"
UPDATE=0
[ "${1:-}" = "--update-golden" ] && UPDATE=1

# V  edges|0  seed  src  mode  p_tram  p_pq  ppn
CONFIGS=(
  "10000 160000 1 1 1 0.999 0.005 1"
  "10000 160000 1 1 1 0.999 0.005 4"
  "10000 160000 1 1 1 0.500 0.005 4"
  "10000 160000 1 1 1 0.999 0.500 4"
  "10000 160000 7 4242 1 0.999 0.005 4"
  "50000 800000 1 1 1 0.999 0.005 4"
  "10000 0 1 0 2 0.999 0.005 1"
  "10000 0 1 0 2 0.999 0.005 4"
  "10000 0 1 5050 2 0.999 0.005 4"
  "40000 0 3 0 2 0.999 0.005 4"
)

# shellcheck disable=SC2086
if ! make sssp_smp ${SSSP_MAKE_ARGS:-} > /tmp/sssp_build.log 2>&1; then
  echo "BUILD FAILED"
  tail -30 /tmp/sssp_build.log
  exit 1
fi

results=""
failures=0
for cfg in "${CONFIGS[@]}"; do
  read -r V E SEED SRC MODE PTRAM PPQ PPN <<< "$cfg"
  out=$(./sssp_smp "$V" "$E" "$SEED" "$SRC" "$MODE" "$PTRAM" "$PPQ" --verify "$PE_FLAG" "$PPN" 2>&1)
  status=$?
  digest=$(echo "$out" | grep -m1 "^VERIFY parallel digest" | sed 's/^VERIFY parallel digest //')
  if [ $status -ne 0 ] || ! echo "$out" | grep -q "^VERIFY PASS"; then
    echo "FAIL (vs serial Dijkstra): $cfg"
    echo "$out" | tail -5 | sed 's/^/    /'
    failures=$((failures + 1))
    digest="<no digest>"
  fi
  # ppn is deliberately excluded from the key: the digest must not depend on it.
  results+="$V $E $SEED $SRC $MODE $PTRAM $PPQ | $digest"$'\n'
done

if [ $UPDATE -eq 1 ]; then
  if [ $failures -ne 0 ]; then
    echo "Refusing to record golden digests while $failures configuration(s) fail."
    exit 1
  fi
  printf '%s' "$results" | sort -u > "$GOLDEN"
  echo "Wrote $(wc -l < "$GOLDEN" | tr -d ' ') golden digests to $GOLDEN"
  exit 0
fi

# Every distinct (graph, parameters) key must map to exactly one digest,
# regardless of how many PEs produced it.
dupes=$(printf '%s' "$results" | sort -u | awk -F'|' '{print $1}' | uniq -d)
if [ -n "$dupes" ]; then
  echo "FAIL: digest depends on PE count for:"
  echo "$dupes" | sed 's/^/    /'
  failures=$((failures + 1))
fi

if [ ! -f "$GOLDEN" ]; then
  echo "No golden file at $GOLDEN; run scripts/verify.sh --update-golden"
  exit 1
fi

if ! diff -u "$GOLDEN" <(printf '%s' "$results" | sort -u); then
  echo "FAIL: digests differ from $GOLDEN"
  echo "If the graph generator changed on purpose, re-record with --update-golden."
  failures=$((failures + 1))
fi

if [ $failures -ne 0 ]; then
  echo "VERIFY GATE FAILED ($failures)"
  exit 1
fi
echo "VERIFY GATE PASSED (${#CONFIGS[@]} configurations)"
