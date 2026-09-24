#!/usr/bin/env bash
# OSM road inputs (road-eu, road-na): a dated Geofabrik extract -> RoutingKit's
# car graph (osm_extract) -> the largest connected component as DIMACS .gr/.co
# (benchmarks/osm_to_dimacs.cpp) in CAMPAIGN/raw, gzipped like USA-road-d.
# prepare_inputs.sbatch then builds .wsg, references and the Morton -z
# relabeling exactly as for road-usa.
#
# Extracts are fetched on a login node first (raw/osm/fetch.sh; 2026-01-01
# snapshots, sha256 in raw/osm/download-sha256.txt). RoutingKit is built in
# deps/RoutingKit (commit recorded below); osm_to_dimacs in CAMPAIGN/bin:
#   g++ -O2 -std=c++17 -o CAMPAIGN/bin/osm_to_dimacs benchmarks/osm_to_dimacs.cpp
#
#   sbatch scripts/frontier/prepare_osm.sbatch CAMPAIGN europe-260101 EU
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-osm
set -euo pipefail
CAMPAIGN=${1:?campaign directory}
EXTRACT=${2:?extract name, e.g. europe-260101}
TAG=${3:?DIMACS tag, e.g. EU}
RK=${ROUTINGKIT:-$(dirname "$CAMPAIGN")/deps/RoutingKit}
RAW=$CAMPAIGN/raw
WORK=$RAW/osm/$EXTRACT
mkdir -p "$WORK"
printf 'job=%s host=%s routingkit=%s\n' "$SLURM_JOB_ID" "$(hostname)" "$(git -C "$RK" rev-parse HEAD)"
sha256sum "$RAW/osm/$EXTRACT.osm.pbf" "$CAMPAIGN/bin/osm_to_dimacs"
if [ ! -s "$WORK/longitude" ]; then
  /usr/bin/time -v "$RK/bin/osm_extract" "$RAW/osm/$EXTRACT.osm.pbf" \
    "$WORK/first_out" "$WORK/head" "$WORK/geo_distance" "$WORK/travel_time" \
    "$WORK/latitude" "$WORK/longitude" > "$WORK/osm_extract.log" 2>&1
fi
tail -n 30 "$WORK/osm_extract.log" | grep -E 'Maximum resident|Elapsed|travel time|length' || true
"$CAMPAIGN/bin/osm_to_dimacs" "$WORK" "$WORK/OSM-road-d.$TAG" | tee "$WORK/osm_to_dimacs.log"
for f in gr co; do
  pigz -p 32 -c "$WORK/OSM-road-d.$TAG.$f" > "$RAW/OSM-road-d.$TAG.$f.gz.part"
  mv "$RAW/OSM-road-d.$TAG.$f.gz.part" "$RAW/OSM-road-d.$TAG.$f.gz"
  rm "$WORK/OSM-road-d.$TAG.$f"
done
# Vertex count for prepare_inputs.sbatch's dimacs step.
sed -n 's/.* vertices=\([0-9]*\) .*/\1/p' "$WORK/osm_to_dimacs.log" > "$RAW/OSM-road-d.$TAG.vertices"
( cd "$RAW" && sha256sum OSM-road-d.$TAG.gr.gz OSM-road-d.$TAG.co.gz ) | tee -a "$RAW/osm/derived-sha256.txt"
echo "OSM COMPLETE $TAG $(cat "$RAW/OSM-road-d.$TAG.vertices") vertices"
