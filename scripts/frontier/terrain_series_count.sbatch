#!/usr/bin/env bash
# Terrain weak-scaling series, step 1: GLO-30 tiles to raw float32, then the
# size of the seed's 8-connected land component in each candidate crop
# (terrain_graph --arcsec 1 --count; nothing is written). The crops nest
# around one seed so the three graphs of the series share their centre:
# about 2.0B vertices (below 2^31, so one-node baselines can run it) for 4
# nodes, about 8.2B for 16 and about 33B for 64, i.e. about 512M vertices per
# node as terrain-ae-z at 16 nodes.
#
#   sbatch scripts/frontier/terrain_series_count.sbatch CAMPAIGN
# CROPS: "LAT0 LAT1 LON0 LON1;..." (default below), SEED="LAT LON" (6 27).
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --qos=debug
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-terrain-count
set -euo pipefail
C=${1:?campaign}
SEED=${SEED:-"6 27"}
CROPS=${CROPS:-"1 12 21 34;0 12 21 34;-5 17 14 40;-6 18 12 40;-35 32 -18 52;-35 38 -18 52"}
module load gcc-native/13.2 gdal/3.12.2 >/dev/null 2>&1
DEM=$C/raw/dem/glo30
F32=$DEM/f32
mkdir -p "$F32"
echo "job=$SLURM_JOB_ID host=$(hostname) seed=$SEED $(gdal_translate --version)"
sha256sum "$C/bin/terrain_graph2"
convert() {
  local base; base=$(basename "$1" .tif)
  [ -s "$2/$base.f32" ] && return
  gdal_translate -q -of ENVI -ot Float32 "$1" "$2/$base.f32.tmp" && mv "$2/$base.f32.tmp" "$2/$base.f32" &&
    rm -f "$2/$base.f32.tmp.aux.xml" "$2/$base.f32.tmp.hdr" || { echo "CONVERT FAILED $base"; return 1; }
}
export -f convert
ls "$DEM"/tiles/*.tif | xargs -P 56 -I{} bash -c 'convert {} '"$F32"
echo "tiles converted: $(ls "$F32" | grep -c '\.f32$') ($(date -Is))"
IFS=';' read -ra crops <<< "$CROPS"
for crop in "${crops[@]}"; do
  # shellcheck disable=SC2086
  echo "CROP $crop: $(OMP_NUM_THREADS=56 OMP_PROC_BIND=spread "$C/bin/terrain_graph2" "$F32" $crop $SEED 6 - --arcsec 1 --count |
    grep -E '^(tiles|component)=' | tr '\n' ' ')"
done
echo "COUNT COMPLETE $SLURM_JOB_ID"
