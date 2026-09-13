#!/usr/bin/env bash
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=64G
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-inputs
set -euo pipefail
ulimit -c 0
CAMPAIGN=${1:?campaign directory}
BIN=$CAMPAIGN/bin
DATA=$CAMPAIGN/graphs
DEPS=${ACIC_BENCH_DEPS:-/u/rao1/acic-comparison-deps}
mkdir -p "$DATA"
export OMP_NUM_THREADS=16
generate() {
  local name=$1 mode=$2 vertices=$3 seed=$4
  [ -s "$DATA/$name.wsg" ] && [ -s "$DATA/$name.meta" ] && return
  "$BIN/prepare_graph" gen "$mode" "$vertices" "$seed" "$DATA/$name.wsg" > "$DATA/$name.meta"
}
generate uniform20 1 1048576 1
generate rmat20 3 1048576 1
generate mesh20 2 1048576 1
generate rmat22 3 4194304 1
generate mesh22 2 4194304 1
generate rmat20-s2 3 1048576 2
generate uniform20-s2 1 1048576 2
if [ ! -s "$DATA/road-ny.meta" ]; then
  gzip -dc "$DEPS/USA-road-d.NY.gr.gz" | "$BIN/prepare_graph" dimacs 264346 0 1 "$DATA/road-ny.wsg" > "$DATA/road-ny.meta"
fi
gzip -dc "$DEPS/com-youtube.ungraph.txt.gz" | "$BIN/prepare_graph" snap 1134890 0 1 "$DATA/youtube.wsg" > "$DATA/youtube.meta"
for file in "$DATA/"*.wsg; do
  "$BIN/reference_gap" "$file" 10 > "${file%.wsg}.reference.txt"
  sha256sum "$file"
done > "$DATA/sha256.txt"
printf 'INPUTS COMPLETE\n'
