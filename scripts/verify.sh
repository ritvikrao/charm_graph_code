#!/usr/bin/env bash
#
# Correctness regression gate for sssp_smp.
#
# Three independent checks run for every configuration:
#
#   1. --verify compares the parallel result against serial Dijkstra over the
#      same graph, in-process. This catches algorithm and runtime bugs.
#
#   2. The resulting digest is compared against scripts/golden_digests.txt.
#      Check 1 alone cannot catch a change to the graph generator, because the
#      parallel and serial paths share it -- both would move together and still
#      agree. The golden file pins the graph itself.
#
#   3. tools/graph_digest, which builds without Charm++ and shares no code path
#      with the solver beyond graphlib itself, must produce the same digest.
#      This is what caught a one-character change to the RNG seeding that moved
#      every generated graph while leaving checks 1 and 2 green, because both
#      of those move together when the generator does.
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
#                  These must be passed on make's command line, which beats
#                  both the Makefile's defaults and config.mk.
#
set -uo pipefail

cd "$(dirname "$0")/.."
GOLDEN="scripts/golden_digests.txt"
PE_FLAG="${SSSP_PE_FLAG:-+ppn}"
UPDATE=0
[ "${1:-}" = "--update-golden" ] && UPDATE=1

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# V  edges|0|path  seed  src  mode  p_tram  p_pq  ppn
# mode: 1 uniform, 2 mesh, 3 rmat, 4 gapbs .wsg (written below from mode 1/3)
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
  "16384 262144 1 0 3 0.999 0.005 1"
  "16384 262144 1 0 3 0.999 0.005 4"
  "16384 262144 5 9 3 0.999 0.005 4"
  "65536 1048576 1 42 3 0.999 0.005 4"
  "0 GRAPH_UNIFORM 1 1 4 0.999 0.005 1"
  "0 GRAPH_UNIFORM 1 1 4 0.999 0.005 4"
  "0 GRAPH_RMAT 1 0 4 0.999 0.005 4"
  "0 GRAPH_MESH 1 0 4 0.999 0.005 4"
)

# shellcheck disable=SC2086
if ! make sssp_smp tools ${SSSP_MAKE_ARGS:-} > "$WORK/build.log" 2>&1; then
  echo "BUILD FAILED"
  tail -30 "$WORK/build.log"
  exit 1
fi

# The file-mode configurations need real files. Writing them here rather than
# committing them keeps the repository free of binary blobs and makes the
# writer part of what the gate exercises: a .wsg the solver reads is a .wsg
# something in this tree produced.
./graph_convert gen 1 10000 16     1 "$WORK/uniform.wsg" > /dev/null || exit 1
./graph_convert gen 3 16384 262144 1 "$WORK/rmat.wsg"    > /dev/null || exit 1
./graph_convert gen 2 10000 0      1 "$WORK/mesh.wsg"    > /dev/null || exit 1

results=""
failures=0
for cfg in "${CONFIGS[@]}"; do
  read -r V E SEED SRC MODE PTRAM PPQ PPN <<< "$cfg"
  key="$V $E $SEED $SRC $MODE $PTRAM $PPQ"
  arg2="$E"
  case "$E" in
    GRAPH_UNIFORM) arg2="$WORK/uniform.wsg" ;;
    GRAPH_RMAT)    arg2="$WORK/rmat.wsg" ;;
    GRAPH_MESH)    arg2="$WORK/mesh.wsg" ;;
  esac

  out=$(./sssp_smp "$V" "$arg2" "$SEED" "$SRC" "$MODE" "$PTRAM" "$PPQ" \
          --verify --timeout 300 "$PE_FLAG" "$PPN" 2>&1)
  status=$?
  digest=$(echo "$out" | grep -m1 "^VERIFY parallel digest" | sed 's/^VERIFY parallel digest //')
  if [ $status -ne 0 ] || ! echo "$out" | grep -q "^VERIFY PASS"; then
    echo "FAIL (vs serial Dijkstra): $cfg"
    echo "$out" | tail -5 | sed 's/^/    /'
    failures=$((failures + 1))
    digest="<no digest>"
  fi

  # Check 3: the standalone tool, which shares no code with the solver beyond
  # graphlib, must agree.
  case "$MODE" in
    1) tool_digest=$(./graph_digest "$V" "$((E / V))" "$SEED" 1 "$SRC") ;;
    2) tool_digest=$(./graph_digest "$V" 0 "$SEED" 2 "$SRC") ;;
    3) tool_digest=$(./graph_digest "$V" "$E" "$SEED" 3 "$SRC") ;;
    4) tool_digest=$(./graph_digest 0 0 "$SEED" 4 "$SRC" "$arg2") ;;
  esac
  if [ "$digest" != "$tool_digest" ] && [ "$digest" != "<no digest>" ]; then
    echo "FAIL (solver vs tools/graph_digest): $cfg"
    echo "    solver: $digest"
    echo "    tool:   $tool_digest"
    failures=$((failures + 1))
  fi

  # ppn is deliberately excluded from the key: the digest must not depend on it.
  results+="$key | $digest"$'\n'
done

if [ $UPDATE -eq 1 ]; then
  if [ $failures -ne 0 ]; then
    echo "Refusing to record golden digests while $failures check(s) fail."
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
