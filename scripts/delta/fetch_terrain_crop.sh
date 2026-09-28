#!/usr/bin/env bash
# The D1 terrain crop on Delta: GLO-30 tiles with south edge in [LAT0, LAT1)
# and west edge in [LON0, LON1), fetched from the public copernicus-dem-30m
# bucket (as scripts/frontier/fetch_glo30.sh) and converted to the raw
# little-endian float32 T x T arrays terrain_graph reads. Delta has no GDAL
# module, so the conversion is rasterio's bundled GDAL writing band 1 as
# float32 -- the bytes gdal_translate -of ENVI -ot Float32 writes. Resumable.
# A tile that 404s is ocean (tiles exist only where there is land).
#
#   scripts/delta/fetch_terrain_crop.sh CAMPAIGN [LAT0 LAT1 LON0 LON1]
set -uo pipefail
ROOT=${1:?campaign}
LAT0=${2:-3} LAT1=${3:-9} LON0=${4:-24} LON1=${5:-31}
DEM=$ROOT/raw/dem/glo30
mkdir -p "$DEM/tiles" "$DEM/f32"
PY=$ROOT/venv/bin/python
name() {  # lat lon -> Copernicus_DSM_COG_10_N03_00_E024_00_DEM
  local lat=$1 lon=$2 ns=N ew=E
  [ "$lat" -lt 0 ] && { ns=S; lat=$(( -lat )); }
  [ "$lon" -lt 0 ] && { ew=W; lon=$(( -lon )); }
  printf 'Copernicus_DSM_COG_10_%s%02d_00_%s%03d_00_DEM' "$ns" "$lat" "$ew" "$lon"
}
for lat in $(seq "$LAT0" $((LAT1 - 1))); do
  for lon in $(seq "$LON0" $((LON1 - 1))); do name "$lat" "$lon"; echo; done
done > "$DEM/crop-tiles.txt"
fetch() {
  local t=$1 dir=$2
  [ -s "$dir/tiles/$t.tif" ] || [ -e "$dir/tiles/$t.absent" ] && return
  code=$(curl -sS --retry 5 -o "$dir/tiles/$t.tif.part" -w '%{http_code}' \
    "https://copernicus-dem-30m.s3.amazonaws.com/$t/$t.tif")
  case $code in
    200) mv "$dir/tiles/$t.tif.part" "$dir/tiles/$t.tif" ;;
    404|403) rm -f "$dir/tiles/$t.tif.part"; touch "$dir/tiles/$t.absent" ;;
    *) rm -f "$dir/tiles/$t.tif.part"; echo "FAILED $t ($code)" ;;
  esac
}
export -f fetch
xargs -P 16 -I{} bash -c 'fetch {} "$0"' "$DEM" < "$DEM/crop-tiles.txt"
"$PY" - "$DEM" <<'PY'
import sys
from pathlib import Path
import numpy as np, rasterio
dem = Path(sys.argv[1])
for tif in sorted((dem/'tiles').glob('*.tif')):
    out = dem/'f32'/(tif.stem + '.f32')
    if out.exists() and out.stat().st_size == 3600*3600*4: continue
    with rasterio.open(tif) as src:
        assert (src.width, src.height) == (3600, 3600), (tif, src.width, src.height)
        a = src.read(1).astype('<f4', copy=False)
    tmp = out.with_suffix('.f32.tmp'); a.tofile(tmp); tmp.rename(out)
print('converted', len(list((dem/'f32').glob('*.f32'))))
PY
echo "tiles: $(ls "$DEM/tiles" | grep -c '\.tif$') present, $(ls "$DEM/tiles" | grep -c '\.absent$') absent, of $(wc -l < "$DEM/crop-tiles.txt")"
( cd "$DEM/tiles" && sha256sum *.tif ) > "$DEM/tiles-sha256.txt"
echo "CROP FETCH DONE"
