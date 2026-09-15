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
# A fourth check runs at the end: sssp_smp_diag, the step 6 diagnosis build,
# must reach the golden digest too. Its counters sit on the relaxation path.
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
#   SSSP_EXTRA_ARGS solver options appended to every run, e.g.
#                  "--flush-policy adaptive". Step 7's gate is that every
#                  mechanism verifies identically with its flag on and off, so
#                  the gate runs once per setting. The golden digests do not
#                  change: they are the answer, not a property of the schedule.
#
set -uo pipefail

# Every test of a run's output below is `grep -q ... <<< "$out"`, never
# `echo "$out" | grep -q ...`. Under pipefail the pipeline form is wrong whenever
# the output is larger than a pipe buffer: grep -q exits at its first match,
# echo dies of SIGPIPE, and pipefail reports the pipeline as failed -- so a
# match reads as no match. A stalled fixture writes ~200 KB, which is exactly
# the case the stall checks exist for: on Anvil the pipeline form missed 7 of 7
# STALL_RESCUE lines that the here-string caught, and the gate went green on a
# fixture that had stalled. Found in 7.6g; see design/step76-default-deadlock.md.
cd "$(dirname "$0")/.."
# Every run below is one process started without a launcher. Inside a Slurm
# allocation LCT otherwise falls through to its `file` PMI backend, which reads
# SLURM_NTASKS and waits forever for ranks that will never start -- on Anvil,
# in a job with --ntasks-per-node=8, the first configuration sat at 0% CPU.
export LCT_PMI_BACKEND="${LCT_PMI_BACKEND:-local}"
GOLDEN="scripts/golden_digests.txt"
PE_FLAG="${SSSP_PE_FLAG:-+ppn}"
read -r -a EXTRA_ARGS <<< "${SSSP_EXTRA_ARGS:-}"
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

# sssp_smp_diag is built here too. It is the binary every structural number in
# the step 6 diagnosis comes from, so it has to be known to solve the same
# graphs to the same answers as the one it is reasoning about; its extra
# counters are on the relaxation path and could perfectly well be wrong.
# shellcheck disable=SC2086
if ! make sssp_smp sssp_smp_diag tools ${SSSP_MAKE_ARGS:-} > "$WORK/build.log" 2>&1; then
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
          --verify --timeout 300 ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} \
          "$PE_FLAG" "$PPN" 2>&1)
  status=$?
  digest=$(echo "$out" | grep -m1 "^VERIFY parallel digest" | sed 's/^VERIFY parallel digest //')
  if [ $status -ne 0 ] || ! grep -q "^VERIFY PASS" <<< "$out"; then
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

  # The controller's histogram is a live-population count: the reduced window
  # cannot hold more updates than exist. A run that says otherwise has computed
  # every threshold from a number that means nothing, and the way that shows up
  # in the field is a run that never finishes.
  if grep -qE "^CONSERVATION VIOLATED|^COARSEN_CLAMPED" <<< "$out"; then
    echo "FAIL (conservation): $cfg"
    echo "$out" | grep -m1 -A2 -E "^CONSERVATION VIOLATED|^COARSEN_CLAMPED" | sed 's/^/    /'
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

# The diagnosis build must agree with the production build wherever both run.
# One configuration per graph family is enough: what could differ is the
# instrumentation, not the mode.
DIAG_CONFIGS=(
  "10000 160000 1 1 1 0.999 0.005 4"
  "10000 0 1 0 2 0.999 0.005 4"
  "16384 262144 1 0 3 0.999 0.005 4"
)
for cfg in "${DIAG_CONFIGS[@]}"; do
  read -r V E SEED SRC MODE PTRAM PPQ PPN <<< "$cfg"
  key="$V $E $SEED $SRC $MODE $PTRAM $PPQ"
  out=$(./sssp_smp_diag "$V" "$E" "$SEED" "$SRC" "$MODE" "$PTRAM" "$PPQ" \
          --verify --timeout 300 ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} \
          "$PE_FLAG" "$PPN" 2>&1)
  digest=$(echo "$out" | grep -m1 "^VERIFY parallel digest" | sed 's/^VERIFY parallel digest //')
  want=$(grep -m1 -F "$key | " "$GOLDEN" | sed 's/^.* | //')
  if ! grep -q "^VERIFY PASS" <<< "$out" || [ "$digest" != "$want" ]; then
    echo "FAIL (sssp_smp_diag): $cfg"
    echo "    diag:   $digest"
    echo "    golden: $want"
    failures=$((failures + 1))
  fi
done

# Progress fixtures: configurations that used to stop making progress and hang.
#
# The controller reduces a window of histo_reduction_width buckets starting at
# the lowest bucket it last saw occupied, so it is blind to work above that
# window's right edge. A bucket width narrow enough that one edge weight spans
# more than the window puts the whole frontier there in a single step -- which
# is what --bucket-width 3 did to rmat22 in the step 7.5 comparisons, where the
# largest edge weight is 1000 and 256 buckets reach only 768 distance units.
# The window then summed to -1 rather than to 0, because the injected source
# update was retired without ever having been created; the "window is empty,
# open the thresholds" rescue did not fire; and the percentile scan pinned both
# thresholds at the window's own origin, where they stayed for the rest of the
# run. See design/step76-progress.md.
#
# Each fixture names a policy in its own flags, so it tests that policy whatever
# SSSP_EXTRA_ARGS says. The timeout is short on purpose: a regression here is a
# hang, and the gate should say so in a minute rather than in five.
#
# V E SEED SRC MODE PTRAM PPQ PPN | flags
# The first three are the step 7.5 policy verbatim, on the smallest sources
# that reproduce its hang: they stop after a handful of updates. The rest
# force the same geometry from a width so narrow that every edge leaves the
# window, over the coarsening path and over a delayed round.
FIELD_POLICY="--flush-policy fixed --flush-interval 1 --bucket-policy fixed --idle-flush off --bucket-width 3"
PROGRESS_CONFIGS=(
  "16384 262144 1 27 3 0.999 0.005 4|$FIELD_POLICY"
  "16384 262144 1 31 3 0.999 0.005 1|$FIELD_POLICY"
  "10000 160000 1 0 1 0.999 0.005 4|$FIELD_POLICY"
  "10000 0 1 0 2 0.999 0.005 4|--bucket-policy fixed --bucket-width 0.001"
  "10000 0 1 0 2 0.999 0.005 4|--bucket-policy adaptive --bucket-width 0.001"
  "10000 0 1 0 2 0.999 0.005 4|--bucket-policy fixed --bucket-width 0.001 --round-delay 2"
  # The clamp fixture. A 200x200 mesh at width 8 clamps at distance 16384
  # against a range near 24000, so the clamp bucket goes live while the band is
  # still wide enough to coarsen. With --coarsen-clamped allow the guard is out
  # of the way, so this is the merge itself under test: unfrozen it strands the
  # clamped counts at 2047/k, the window pins there and the run hangs until the
  # timeout, three times out of three. Frozen, bucket 2047 is an overflow slot
  # that no merge touches, increment and decrement both land on it, and the
  # same run converges in about 0.05 s. This is the one configuration in the
  # gate that fails if --clamp-freeze stops working, so it names it.
  "40000 0 1 0 2 0.999 0.005 4|--bucket-policy adaptive --bucket-width 8 --coarsen-clamped allow --clamp-freeze on"
  # The range fixture. The same 200x200 mesh binned at width 1 puts every
  # distance past the clamp, so the overflow slot takes essentially the whole
  # run and --range-extend has to raise the clamp a dozen times to recover an
  # ordering. Three things are on test and each has already failed once: the
  # creation-time flag, without which a raised clamp strands the counts exactly
  # as the unfrozen merge did; the arrivals trigger, without which the rule
  # reads its own raise as having done nothing and runs the scale away; and
  # keeping the overflow index out of the rescale, without which the window
  # origin is divided to 2047/k and road-ny hangs to the timeout with a wrong
  # digest. A PASS here is all three.
  "40000 0 1 0 2 0.999 0.005 4|--bucket-policy adaptive --bucket-width 1 --range-extend on"
)
# The queue-order fixture, 7.6g. At bucket scale 1 the slice [2047, 2048) widths
# is flagged as overflow and keeps bucket 2047 for life; an update admitted to
# the heap from it while the threshold was 2047 stays on top once a coarsening
# drops the threshold, and process_heap() stops there with admissible work
# behind it. The window then pins at floor(2047 / scale) -- the shipped
# default's deadlock. A 300x300 mesh at width 16 with the two-tier limit pinned
# low coarsens just before crossing the clamp.
#
# It is NOT deterministic, so it is repeated, and the count is sized from a
# measurement rather than a guess. With --pq-overflow-last off, run one at a
# time on an Anvil node: source 12345 needed a rescue in 6 of 20 runs and
# source 777 in 3 of 20 (144/480 and 128/480 with 24 running at once). Sixteen
# of the first and four of the second miss a regression with probability about
# 0.7^16 * 0.85^4 = 0.002. The first version ran twelve over three sources,
# estimated its miss rate from the concurrent numbers, and passed once with the
# repair off -- which at the sequential rate it would do about one time in
# nine. With it on: 0 stalls in 180 concurrent runs. Any STALL_RESCUE fails the
# gate below, so a regression fails in 32 rounds, not at the timeout.
for rep in $(seq 1 16); do
  PROGRESS_CONFIGS+=("90000 0 1 12345 2 0.999 0.005 4|--bucket-policy adaptive --bucket-width 16 --two-tier-absolute 1600 --clamp-freeze on")
done
for rep in 1 2 3 4; do
  PROGRESS_CONFIGS+=("90000 0 1 777 2 0.999 0.005 4|--bucket-policy adaptive --bucket-width 12 --two-tier-absolute 1600 --clamp-freeze on")
done
progress_run=0
for entry in "${PROGRESS_CONFIGS[@]}"; do
  cfg="${entry%%|*}"
  read -r -a fixture_args <<< "${entry#*|}"
  # --combine hold and --bucket-policy adaptive are mutually exclusive by
  # design: CombiningHold keeps per-bucket reference lists that a merge cannot
  # move, and the solver rejects the pair rather than silently picking one. The
  # coarsening fixture names the policy itself, so under SSSP_EXTRA_ARGS
  # "--combine hold" there is no run to make; skip it rather than record a
  # failure for a combination that cannot exist.
  case " ${SSSP_EXTRA_ARGS:-} " in
    *" --combine hold "*|*"--combine=hold"*) holding=1 ;;
    *) holding=0 ;;
  esac
  case " ${entry#*|} " in
    *" --bucket-policy adaptive "*) coarsening=1 ;;
    *) coarsening=0 ;;
  esac
  if [ "$holding" = 1 ] && [ "$coarsening" = 1 ]; then
    echo "skip (combining excludes bucket coarsening): $cfg"
    continue
  fi
  read -r V E SEED SRC MODE PTRAM PPQ PPN <<< "$cfg"
  progress_run=$((progress_run + 1))
  # These sources are not in the golden file -- they are chosen for the hang,
  # not for the generator -- so the answer is checked the strongest way
  # available in-process, against serial Dijkstra over the same graph.
  out=$(./sssp_smp "$V" "$E" "$SEED" "$SRC" "$MODE" "$PTRAM" "$PPQ" \
          --verify --timeout 60 ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} \
          "${fixture_args[@]}" "$PE_FLAG" "$PPN" 2>&1)
  status=$?
  if [ $status -ne 0 ] || ! grep -q "^VERIFY PASS" <<< "$out"; then
    echo "FAIL (progress fixture): $cfg ${entry#*|}"
    echo "$out" | grep -m3 "^PROGRESS_STALL .* main" | sed 's/^/    /'
    echo "$out" | tail -3 | sed 's/^/    /'
    failures=$((failures + 1))
  fi
  # A run that finishes but reported a stall on the way is still a regression:
  # it means the controller lost the frontier and only the rescue got it back.
  # STALL_RESCUE is in this list deliberately. It restores progress and the run
  # then passes, so without failing on it here a fixture that deadlocks would
  # go green and the defect underneath would stop being visible. The rescue is
  # for production runs; needing it in the gate is a regression.
  if grep -qE "^PROGRESS_STALL|^CONSERVATION VIOLATED|^COARSEN_CLAMPED|^STALL_RESCUE" <<< "$out"; then
    echo "FAIL (progress fixture reported a stall): $cfg ${entry#*|}"
    echo "$out" | grep -m2 -E "^PROGRESS_STALL|^CONSERVATION VIOLATED|^COARSEN_CLAMPED|^STALL_RESCUE" | sed 's/^/    /'
    failures=$((failures + 1))
  fi
done

if [ $failures -ne 0 ]; then
  echo "VERIFY GATE FAILED ($failures)"
  exit 1
fi
echo "VERIFY GATE PASSED (${#CONFIGS[@]} configurations, plus ${#DIAG_CONFIGS[@]} on sssp_smp_diag and $progress_run progress fixtures)${SSSP_EXTRA_ARGS:+ with $SSSP_EXTRA_ARGS}"
