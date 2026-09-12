#!/usr/bin/env bash
#
# Step 6 of the SC27 plan: the four scale-free hypotheses, each as an A/B with
# everything else held fixed. See design/scale-free-h1..h4.md for what each one
# is testing and what the answers were.
#
# Two binaries, deliberately:
#
#   sssp_smp       the production build. Every wall-clock number comes from
#                  here, because the diagnosis build's counters sit on the
#                  relaxation path and would be timing themselves.
#   sssp_smp_diag  everything structural -- bucket occupancy, per-PE work, the
#                  combining ceiling. None of it depends on the clock, so it is
#                  safe to measure it in a slower binary.
#
# Usage:
#   scripts/diagnose.sh <h1|h2|h3|h4|all> [outdir]
#
# Environment:
#   SSSP_SCALE   log2 of the vertex count for the generated graphs. Default 16,
#                which runs on a login node in seconds. The reported runs used
#                20 on a compute node.
#   SSSP_PPN     worker threads per process. Default 4.
#   SSSP_LAUNCH  prefix for every run, e.g. "srun". Default empty.
#   SSSP_REPS    repetitions per timed configuration. Default 3; the median is
#                what gets reported.
#   SSSP_PE_FLAG "+ppn" (Reconverse) or "+p" (classic multicore). Default +ppn.
#   SSSP_CHARM_FLAGS extra runtime flags appended to every run, e.g.
#                "+setcpuaffinity". Pin these on a shared node or the timed
#                comparisons measure the scheduler.
#
set -uo pipefail
cd "$(dirname "$0")/.."

EXPERIMENT="${1:-all}"
OUT="${2:-diag-out}"
SCALE="${SSSP_SCALE:-16}"
PPN="${SSSP_PPN:-4}"
LAUNCH="${SSSP_LAUNCH:-}"
REPS="${SSSP_REPS:-3}"
PE_FLAG="${SSSP_PE_FLAG:-+ppn}"
TIMEOUT="${SSSP_TIMEOUT:-600}"
read -r -a CHARM_FLAGS <<< "${SSSP_CHARM_FLAGS:-}"

V=$((1 << SCALE))
E=$((V * 16))
# The mesh wants a perfect square and the same order of magnitude, not the same
# edge count: its degree is 4 whatever we do, and matching |E| instead of |V|
# would make it a different-sized problem in the only dimension that matters
# for a diameter argument.
MESH_SIDE=$(python3 -c "import math;print(int(math.isqrt($V)))")
MESH_V=$((MESH_SIDE * MESH_SIDE))

mkdir -p "$OUT"

if ! make sssp_smp sssp_smp_diag graph_convert > "$OUT/build.log" 2>&1; then
  echo "BUILD FAILED"; tail -30 "$OUT/build.log"; exit 1
fi

# The source vertex is not a free choice on a scale-free graph: a third of an
# RMAT graph's vertices have no out-edges, and a source with none never
# satisfies the convergence test at all -- the run sits until --timeout. Ask
# the generator for one rather than picking by hand, so the configuration is
# reproducible and the same at every PE count. The rule is in
# tools/graph_convert: lowest-numbered vertex of at least mean out-degree.
pick_source() { ./graph_convert source "$3" "$1" "$2" 1 2>> "$OUT/graphs.txt"; }

: > "$OUT/graphs.txt"
UNIFORM_SRC=$(pick_source "$V" 16 1)
MESH_SRC=$(pick_source "$MESH_V" 0 2)
RMAT_SRC=$(pick_source "$V" "$E" 3)

# name  V  arg2  mode  source
GRAPHS=(
  "uniform $V $E 1 $UNIFORM_SRC"
  "mesh    $MESH_V 0 2 $MESH_SRC"
  "rmat    $V $E 3 $RMAT_SRC"
)
cat "$OUT/graphs.txt"

# Pull one number out of a run log. Prints nothing if the line is absent, which
# is what a crashed or timed-out run looks like.
field() { sed -n "s/^$2//p" "$1" | head -1 | tr -d ' '; }

median() { sort -g | awk '{a[NR]=$1} END{ if(NR==0) print ""; else if(NR%2) print a[(NR+1)/2]; else print (a[NR/2]+a[NR/2+1])/2 }'; }

# run <binary> <logfile> <V> <arg2> <src> <mode> <extra args...>
run() {
  local bin="$1" log="$2" v="$3" arg2="$4" src="$5" mode="$6"; shift 6
  # shellcheck disable=SC2086
  $LAUNCH "./$bin" "$v" "$arg2" 1 "$src" "$mode" 0.999 0.005 \
      --timeout "$TIMEOUT" "$@" "$PE_FLAG" "$PPN" \
      ${CHARM_FLAGS[@]+"${CHARM_FLAGS[@]}"} > "$log" 2>&1
  local status=$?
  if [ $status -ne 0 ] || ! grep -q "^Compute time:" "$log"; then
    echo "    RUN FAILED ($bin $v $arg2 $src mode=$mode $*) -- see $log" >&2
    return 1
  fi
  return 0
}

# Median compute time over REPS repetitions of one configuration.
timed() {
  local tag="$1" v="$2" arg2="$3" src="$4" mode="$5"; shift 5
  local r times=""
  for r in $(seq 1 "$REPS"); do
    if run sssp_smp "$OUT/$tag.rep$r.log" "$v" "$arg2" "$src" "$mode" "$@"; then
      times+="$(field "$OUT/$tag.rep$r.log" 'Compute time: ')"$'\n'
    fi
  done
  printf '%s' "$times" | median
}

# The width the code derives from |V| today: log V for the random-graph modes,
# sqrt V for the mesh. H1 is the claim that neither reads anything about the
# distances the graph actually produces, so the sweep is expressed as multiples
# of whatever the rule currently picks.
natural_width() {
  python3 -c "import math;print(math.sqrt($1) if $2==2 else math.log($1))"
}

# ---------------------------------------------------------------- H1 ---------
h1() {
  echo "== H1: does the bucket rule give the controller any resolution? =="
  mkdir -p "$OUT/h1"
  for g in "${GRAPHS[@]}"; do
    read -r name v arg2 mode src <<< "$g"
    echo "  $name: bucket occupancy profile"
    run sssp_smp_diag "$OUT/h1/$name.diag.log" "$v" "$arg2" "$src" "$mode" \
        --diag "$OUT/h1/$name"
  done

  echo "  bucket-width sweep (median of $REPS, seconds)"
  : > "$OUT/h1/sweep.tsv"
  printf 'graph\tmultiplier\twidth\tcompute_s\trejected_per_edge\tthreshold_changes\treductions\n' \
      >> "$OUT/h1/sweep.tsv"
  for g in "${GRAPHS[@]}"; do
    read -r name v arg2 mode src <<< "$g"
    local base; base=$(natural_width "$v" "$mode")
    for mult in 0.0625 0.125 0.25 0.5 1 2 4 8 16; do
      local width; width=$(python3 -c "print($base*$mult)")
      local tag="h1/${name}_w$mult"
      local t; t=$(timed "$tag" "$v" "$arg2" "$src" "$mode" --bucket-width "$width")
      local log="$OUT/$tag.rep1.log"
      printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$mult" "$width" "$t" \
          "$(field "$log" 'Rejected updates normalized to |E|: ')" \
          "$(field "$log" 'Number of threshold changes: ')" \
          "$(field "$log" 'Number of reductions: ')" >> "$OUT/h1/sweep.tsv"
      echo "    $name x$mult (width $width): ${t:-FAILED} s"
    done
  done
}

# ---------------------------------------------------------------- H2 ---------
h2() {
  echo "== H2: is the redundant traffic going to hubs, and can combining catch it? =="
  mkdir -p "$OUT/h2"
  printf 'graph\tbufsize\tedges\trejected_per_edge\tbatch_items\tbatch_absorbable\n' \
      > "$OUT/h2/absorb.tsv"
  for g in "${GRAPHS[@]}"; do
    read -r name v arg2 mode src <<< "$g"
    # The absorb rate is a property of how many items share a buffer, so the
    # buffer size is the independent variable: it tells step 7 how large a hold
    # has to be before combining pays for itself.
    for bufsize in 128 512 2048; do
      local log="$OUT/h2/${name}_buf$bufsize.log"
      run sssp_smp_diag "$log" "$v" "$arg2" "$src" "$mode" --bufsize "$bufsize" || continue
      printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$bufsize" \
          "$(field "$log" 'Actual edges: ')" \
          "$(field "$log" 'Rejected updates normalized to |E|: ')" \
          "$(sed -n 's/^Batch items: \([0-9]*\),.*/\1/p' "$log" | head -1)" \
          "$(sed -n 's/^Batch items: [0-9]*, absorbable within a batch: \([0-9]*\).*/\1/p' "$log" | head -1)" \
          >> "$OUT/h2/absorb.tsv"
      echo "    $name bufsize=$bufsize: $(grep -m1 '^Batch items:' "$log")"
    done
  done
}

# ---------------------------------------------------------------- H3 ---------
h3() {
  echo "== H3: does a contiguous 1-D partitioning of a power law imbalance? =="
  mkdir -p "$OUT/h3"
  local saved="$PPN"
  # --partition-jitter 0 on every graph. Without it this is not a comparison:
  # the uniform mode draws its partition sizes at random within +-20% of V/N
  # while the mesh and RMAT divide evenly, so the uniform graph would be
  # carrying an injected skew larger than anything the power law produces.
  for g in "${GRAPHS[@]}"; do
    read -r name v arg2 mode src <<< "$g"
    for pes in 1 2 4 8 16 32; do
      PPN="$pes"
      local log="$OUT/h3/${name}_pe$pes.log"
      run sssp_smp_diag "$log" "$v" "$arg2" "$src" "$mode" \
          --partition-jitter 0 || continue
      echo "    $name on $pes PEs: $(grep -c '^DIAG_PE' "$log") PE rows"
    done
  done
  PPN="$saved"

  # Calibration: an imbalance measurement that reads 1.0 everywhere is
  # indistinguishable from a broken one. The uniform mode's jitter is a known
  # quantity of injected skew, so turning it up says what the instrument reads
  # when imbalance is definitely there -- and what that imbalance costs.
  echo "  partition-jitter calibration on the uniform graph"
  printf 'jitter_percent\tcompute_s\trejected_per_edge\n' > "$OUT/h3/jitter.tsv"
  read -r name v arg2 mode src <<< "${GRAPHS[0]}"
  for jitter in 0 10 20 40; do
    local tag="h3/jitter$jitter"
    local t; t=$(timed "$tag" "$v" "$arg2" "$src" "$mode" --partition-jitter "$jitter")
    run sssp_smp_diag "$OUT/h3/jitter${jitter}_diag.log" "$v" "$arg2" "$src" \
        "$mode" --partition-jitter "$jitter"
    printf '%s\t%s\t%s\n' "$jitter" "$t" \
        "$(field "$OUT/$tag.rep1.log" 'Rejected updates normalized to |E|: ')" \
        >> "$OUT/h3/jitter.tsv"
    echo "    jitter=${jitter}%: ${t:-FAILED} s"
  done
}

# ---------------------------------------------------------------- H4 ---------
h4() {
  echo "== H4: does the tail advance only at the controller's cadence? =="
  mkdir -p "$OUT/h4"
  for g in "${GRAPHS[@]}"; do
    read -r name v arg2 mode src <<< "$g"
    run sssp_smp_diag "$OUT/h4/$name.diag.log" "$v" "$arg2" "$src" "$mode" \
        --diag "$OUT/h4/$name"
  done

  # The specific mechanism, not just the cadence. htram's idle-triggered flush
  # is compiled out and its periodic timer is off, so a partly-filled
  # aggregation buffer leaves only by filling to bufSize or by catching one of
  # the per-chare tflush draws. In the tail there is not enough traffic left to
  # fill anything, so the draw is the only way out -- and its rate is a knob.
  echo "  flush-interval sweep (median of $REPS, seconds)"
  printf 'graph\tflush_interval\tcompute_s\treductions\trejected_per_edge\ttram_messages\n' \
      > "$OUT/h4/flush.tsv"
  for g in "${GRAPHS[@]}"; do
    read -r name v arg2 mode src <<< "$g"
    for interval in 1 2 5 10 20; do
      local tag="h4/${name}_f$interval"
      local t; t=$(timed "$tag" "$v" "$arg2" "$src" "$mode" --flush-interval "$interval")
      local log="$OUT/$tag.rep1.log"
      printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$interval" "$t" \
          "$(field "$log" 'Number of reductions: ')" \
          "$(field "$log" 'Rejected updates normalized to |E|: ')" \
          "$(sed -n 's/^TRAM messages: \([0-9]*\),.*/\1/p' "$log" | head -1)" \
          >> "$OUT/h4/flush.tsv"
      echo "    $name flush every ~$interval rounds: ${t:-FAILED} s"
    done
  done

  echo "  round-delay sweep (median of $REPS, seconds)"
  printf 'graph\tround_delay_ms\tcompute_s\treductions\trejected_per_edge\n' \
      > "$OUT/h4/delay.tsv"
  for g in "${GRAPHS[@]}"; do
    read -r name v arg2 mode src <<< "$g"
    for delay in 0 0.25 0.5 1 2 4; do
      local tag="h4/${name}_d$delay"
      local t; t=$(timed "$tag" "$v" "$arg2" "$src" "$mode" --round-delay "$delay")
      local log="$OUT/$tag.rep1.log"
      printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$delay" "$t" \
          "$(field "$log" 'Number of reductions: ')" \
          "$(field "$log" 'Rejected updates normalized to |E|: ')" \
          >> "$OUT/h4/delay.tsv"
      echo "    $name delay=${delay}ms: ${t:-FAILED} s"
    done
  done
}

echo "scale=2^$SCALE (V=$V, E=$E; mesh ${MESH_SIDE}x${MESH_SIDE}), ppn=$PPN, reps=$REPS"
case "$EXPERIMENT" in
  h1) h1 ;;
  h2) h2 ;;
  h3) h3 ;;
  h4) h4 ;;
  all) h1; h2; h3; h4 ;;
  *) echo "Usage: scripts/diagnose.sh <h1|h2|h3|h4|all> [outdir]"; exit 1 ;;
esac
echo "Results under $OUT/. Summarise with scripts/diag_report.py $OUT"
