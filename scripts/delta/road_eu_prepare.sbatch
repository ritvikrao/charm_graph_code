#!/usr/bin/env bash
# Recreate the campaign's OSM Europe graph from the public 2026-01-01 extract.
# Stages are separate because cpu-interactive has a one-hour limit.
# sbatch --output=ROOT/logs/prepare-%j.out road_eu_prepare.sbatch ROOT build|extract|convert|canonical|reorder
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-eu-input
set -euo pipefail
ulimit -c 0
ROOT=${1:?HDD campaign}
STAGE=${2:?preparation stage}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
RK=$ROOT/deps/RoutingKit
RAW=$ROOT/raw
BIN=$ROOT/bin
DATA=$ROOT/graphs
PY=$ROOT/venv/bin/python
mkdir -p "$BIN" "$DATA" "$ROOT/logs"
export PYTHONNOUSERSITE=1 OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=6
printf 'job=%s stage=%s host=%s\n' "$SLURM_JOB_ID" "$STAGE" "$(hostname)"
case $STAGE in
  build)
    test "$(git -C "$RK" rev-parse HEAD)" = 54d49bb0cdea56dde182357522e4e86a03c57852
    make -C "$RK" -j8 bin/osm_extract > "$ROOT/logs/routingkit-build.log" 2>&1
    g++ -O2 -std=c++17 "$APP/benchmarks/osm_to_dimacs.cpp" -o "$BIN/osm_to_dimacs"
    g++ -O3 -std=c++17 -DGRAPH_GEN_STANDALONE -I"$APP" -I"$APP/benchmarks" \
      "$APP/benchmarks/prepare_graph.cpp" -o "$BIN/prepare_graph"
    g++ -O3 -std=c++17 -fopenmp -I"$HOME/acic-comparison-deps/gapbs/src" -I"$APP/benchmarks" \
      "$APP/benchmarks/reference_gap.cpp" -o "$BIN/reference_gap"
    { git -C "$RK" rev-parse HEAD; git -C "$APP" rev-parse HEAD;
      g++ --version | head -n 1; sha256sum "$RK/bin/osm_extract" "$BIN/"*;
    } > "$ROOT/logs/input-build-manifest.txt"
    ;;
  extract)
    test -f "$RAW/osm/download.complete"
    WORK=$RAW/osm/extract-$SLURM_JOB_ID
    mkdir "$WORK"
    /usr/bin/time -v "$RK/bin/osm_extract" "$RAW/osm/europe-260101.osm.pbf" \
      "$WORK/first_out" "$WORK/head" "$WORK/geo_distance" "$WORK/travel_time" \
      "$WORK/latitude" "$WORK/longitude" > "$WORK/osm_extract.log" 2>&1
    touch "$WORK/complete"
    ln -s "extract-$SLURM_JOB_ID" "$RAW/osm/europe-260101"
    tail -n 25 "$WORK/osm_extract.log"
    ;;
  convert)
    WORK=$RAW/osm/europe-260101
    test -f "$WORK/complete"
    "$BIN/osm_to_dimacs" "$WORK" "$WORK/OSM-road-d.EU" | tee "$WORK/osm_to_dimacs.log"
    for EXT in gr co; do
      pigz -p32 -c "$WORK/OSM-road-d.EU.$EXT" > "$RAW/OSM-road-d.EU.$EXT.gz.part"
      mv "$RAW/OSM-road-d.EU.$EXT.gz.part" "$RAW/OSM-road-d.EU.$EXT.gz"
    done
    sed -n 's/.* vertices=\([0-9]*\) .*/\1/p' "$WORK/osm_to_dimacs.log" > "$RAW/OSM-road-d.EU.vertices"
    sha256sum "$RAW/OSM-road-d.EU."*.gz > "$RAW/osm/derived-sha256.txt"
    ;;
  canonical)
    gzip -dc "$RAW/OSM-road-d.EU.gr.gz" |
      "$BIN/prepare_graph" dimacs "$(cat "$RAW/OSM-road-d.EU.vertices")" 0 1 \
        "$DATA/road-eu.wsg.part" > "$DATA/road-eu.meta.part"
    mv "$DATA/road-eu.wsg.part" "$DATA/road-eu.wsg"
    mv "$DATA/road-eu.meta.part" "$DATA/road-eu.meta"
    "$BIN/reference_gap" "$DATA/road-eu.wsg" 6 any-range > "$DATA/road-eu.reference.part"
    mv "$DATA/road-eu.reference.part" "$DATA/road-eu.reference.txt"
    cat "$DATA/road-eu.meta" "$DATA/road-eu.reference.txt"
    ;;
  reorder)
    "$PY" "$APP/benchmarks/reorder_graph.py" "$ROOT" road-eu road-eu-z \
      coords "$RAW/OSM-road-d.EU.co.gz" --reference-workers 2
    sha256sum "$DATA/road-eu.wsg" "$DATA/road-eu-z.wsg" > "$DATA/sha256.txt"
    cat "$DATA/road-eu-z.meta" "$DATA/road-eu-z.reference.txt"
    ;;
  *) echo "Unknown preparation stage: $STAGE" >&2; exit 2 ;;
esac
touch "$ROOT/logs/$STAGE.complete"
echo "COMPLETE $STAGE"
