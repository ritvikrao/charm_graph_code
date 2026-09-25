#!/usr/bin/env bash
# terrain-ae-z: Copernicus DEM GLO-90 land south of 50N as an 8-neighbour
# walking-time graph (benchmarks/terrain_graph.cpp), the 8-connected land
# component of Afro-Eurasia, Morton ids, wide .wsg (past 2^32 vertices).
#
# 1. Tiles (raw/dem/glo90/tiles, from raw/dem/glo90/fetch.sh on a login node)
#    to raw float32 with gdal_translate, 56 at a time.
# 2. terrain_graph over the region, written into a widely striped file.
# 3. CAMPAIGN/graphs/NAME.{wsg,meta,sources,build.log}. No reference here: one
#    node cannot hold the graph, so references come from certified ACIC runs
#    (terrain_reference.sbatch).
#
#   sbatch scripts/frontier/prepare_terrain.sbatch CAMPAIGN [NAME]
# REGION="LAT0 LAT1 LON0 LON1" (default -50 50 -20 180), SEED="LAT LON"
# (default 40 75, the Tian Shan: inside the Afro-Eurasian component).
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-terrain
set -euo pipefail
C=${1:?campaign}
NAME=${2:-terrain-ae-z}
REGION=${REGION:-"-50 50 -20 180"}
SEED=${SEED:-"40 75"}
module load gcc-native/13.2 gdal/3.12.2 >/dev/null 2>&1
DEM=$C/raw/dem/glo90
F32=$DEM/f32
mkdir -p "$F32"
echo "job=$SLURM_JOB_ID host=$(hostname) name=$NAME region=$REGION seed=$SEED $(gdal_translate --version)"
sha256sum "$C/bin/terrain_graph"
convert() {
  local base; base=$(basename "$1" .tif)
  [ -s "$2/$base.f32" ] && return
  gdal_translate -q -of ENVI -ot Float32 "$1" "$2/$base.f32.tmp" && mv "$2/$base.f32.tmp" "$2/$base.f32" &&
    rm -f "$2/$base.f32.tmp.aux.xml" "$2/$base.f32.tmp.hdr" || { echo "CONVERT FAILED $base"; return 1; }
}
export -f convert
ls "$DEM"/tiles/*.tif | xargs -P 56 -I{} bash -c 'convert {} '"$F32"
echo "tiles converted: $(ls "$F32" | grep -c '\.f32$') ($(date -Is))"
OUT=$C/graphs/$NAME
rm -f "$OUT.wsg.tmp"
lfs setstripe -c 64 -S 16M "$OUT.wsg.tmp"
# shellcheck disable=SC2086
OMP_NUM_THREADS=56 OMP_PROC_BIND=spread /usr/bin/time -v "$C/bin/terrain_graph" "$F32" $REGION $SEED 6 "$OUT.wsg.tmp" \
  > "$OUT.build.log" 2> "$OUT.build.err" || { cat "$OUT.build.err"; echo "TERRAIN FAILED"; exit 1; }
cat "$OUT.build.log"
grep -E 'Maximum resident|Elapsed' "$OUT.build.err"
mv "$OUT.wsg.tmp" "$OUT.wsg"
grep '^vertices=.*riken_denominator' "$OUT.build.log" | tail -1 > "$OUT.meta"
sed -n 's/^sources //p' "$OUT.build.log" > "$OUT.sources"
ls -la "$OUT.wsg"
echo "TERRAIN COMPLETE $NAME $(cat "$OUT.meta")"
