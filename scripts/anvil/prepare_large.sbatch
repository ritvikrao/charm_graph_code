#!/usr/bin/env bash
# Larger inputs for 7.6f2, so that 8-16 node allocations do real work per PE.
# Every graph in the first two waves solves in under half a second even at 16
# nodes (design/step76-policy.md), which measures policy in a regime dominated
# by communication and control.
#
# Same preparation as benchmarks/prepare.sbatch: canonical undirected,
# minimum-parallel-edge .wsg, independent GAPBS-reader Dijkstra references.
# Six sources each (two tuning, four held-out), which is all --mode policy
# reads. road-usa runs its reference with any-range: its distances may exceed
# the binary32 range that guard exists for, and this set is ACIC-only.
#
#   sbatch scripts/anvil/prepare_large.sbatch /anvil/scratch/$USER/acic/campaign [names]
#
# names (comma-separated) defaults to the 7.6f2 set. rmat27 (7.6o) is not in
# it: 4.2B edges, about twice rmat26's 43 GB peak, and it also writes the
# Galois .gr copy for the Gluon arm here rather than in prepare_gluon.sbatch.
# Neither is road-usa-w4 (IPDPS sprint), which also writes its .gr here. Run
# one small input on the shared partition by calling this script with bash
# inside a smaller allocation, e.g.
#   sbatch -p shared -c 8 --mem=24G -t 40 -A asc050025 \
#     --wrap "bash scripts/anvil/prepare_large.sbatch CAMPAIGN road-usa-w4"
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --time=01:15:00
#SBATCH --job-name=acic-large-inputs
set -uo pipefail
ulimit -c 0
module load gcc/11.2.0 python/3.9.5 >/dev/null 2>&1
CAMPAIGN=${1:?campaign directory}
BIN=$CAMPAIGN/bin
DATA=$CAMPAIGN/graphs
DEPS=${ACIC_BENCH_DEPS:-$(dirname "$CAMPAIGN")/deps}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
mkdir -p "$DATA"
export OMP_NUM_THREADS=${OMP_NUM_THREADS:-16}

generate() {
  local name=$1 mode=$2 vertices=$3 seed=$4
  [ -s "$DATA/$name.wsg" ] && [ -s "$DATA/$name.meta" ] && return
  /usr/bin/time -v "$BIN/prepare_graph" gen "$mode" "$vertices" "$seed" "$DATA/$name.wsg" \
    > "$DATA/$name.meta" 2> "$DATA/$name.prepare.log"
}
reference() {
  local name=$1; shift
  [ -s "$DATA/$name.reference.txt" ] && return
  "$BIN/reference_gap" "$DATA/$name.wsg" 6 "$@" > "$DATA/$name.reference.tmp" 2> "$DATA/$name.reference.log" &&
    mv "$DATA/$name.reference.tmp" "$DATA/$name.reference.txt"
}
prepare_one() {
  local name=$1
  case $name in
    mesh24)    generate mesh24 2 16777216 1 ;;
    mesh26)    generate mesh26 2 67108864 1 ;;
    rmat25)    generate rmat25 3 33554432 1 ;;
    rmat26)    generate rmat26 3 67108864 1 ;;
    rmat27)    generate rmat27 3 134217728 1 ;;
    uniform25) generate uniform25 1 33554432 1 ;;
    road-usa)
      [ -s "$DATA/road-usa.meta" ] || gzip -dc "$DEPS/USA-road-d.USA.gr.gz" |
        /usr/bin/time -v "$BIN/prepare_graph" dimacs 23947347 0 1 "$DATA/road-usa.wsg" \
          > "$DATA/road-usa.meta" 2> "$DATA/road-usa.prepare.log" ;;
    road-usa-w4)
      # IPDPS sprint: road-usa's topology with each native weight divided by
      # four, rounded up, so every distance stays below 2^24 and RIKEN's
      # binary32 path is exact. The reference below keeps the guard on.
      [ -s "$DATA/road-usa-w4.meta" ] || gzip -dc "$DEPS/USA-road-d.USA.gr.gz" |
        awk '$1 == "a" { $4 = int(($4 + 3) / 4) } { print }' |
        /usr/bin/time -v "$BIN/prepare_graph" dimacs 23947347 0 1 "$DATA/road-usa-w4.wsg" \
          > "$DATA/road-usa-w4.meta" 2> "$DATA/road-usa-w4.prepare.log" ;;
    orkut)
      [ -s "$DATA/orkut.meta" ] || gzip -dc "$DEPS/com-orkut.ungraph.txt.gz" |
        /usr/bin/time -v "$BIN/prepare_graph" snap 3072441 0 1 "$DATA/orkut.wsg" \
          > "$DATA/orkut.meta" 2> "$DATA/orkut.prepare.log" ;;
  esac || { echo "PREPARE FAILED $name"; return 1; }
  if [ "$name" = road-usa ]; then reference "$name" any-range; else reference "$name"; fi ||
    { echo "REFERENCE FAILED $name"; return 1; }
  if { [ "$name" = rmat27 ] || [ "$name" = road-usa-w4 ]; } && [ ! -e "$DATA/$name.gr.done" ]; then
    python3 "$APP/benchmarks/to_galois.py" "$DATA/$name.wsg" && touch "$DATA/$name.gr.done" ||
      { echo "CONVERSION FAILED $name"; return 1; }
  fi
  echo "PREPARED $name $(date -Is)"
}
export -f generate reference prepare_one
export BIN DATA DEPS APP
names=$(echo "${2:-mesh24,mesh26,rmat25,rmat26,uniform25,road-usa,orkut}" | tr , " ")
# All seven at once: the largest (rmat26) peaks near 50 GB, well inside 257 GB.
# Serial rmat22 took 25 s to generate and 21 s for references (login node);
# rmat26 is 16x that, ~20 min, so 75 min covers contention. Reruns resume.
printf '%s\n' $names | xargs -P 7 -I{} bash -c 'prepare_one {}'
( cd "$DATA" && for n in $names; do [ -s "$n.wsg" ] && sha256sum "$n.wsg"; done ) >> "$DATA/sha256-large.txt"
sort -u -k2,2 -o "$DATA/sha256-large.txt" "$DATA/sha256-large.txt"
for n in $names; do printf '%-10s %s' "$n" "$(cat "$DATA/$n.meta" 2>/dev/null)"; grep -h 'Maximum resident' "$DATA/$n.prepare.log" 2>/dev/null; done
printf 'LARGE INPUTS COMPLETE\n'
