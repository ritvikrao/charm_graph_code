#!/usr/bin/env bash
# Copernicus DEM GLO-30 (1 arc-second DSM, public AWS open-data bucket
# copernicus-dem-30m), every tile with its south edge in [-35, 38) and west
# edge in [-18, 52): Africa, Arabia and the Levant, the superset of the nested
# terrain series crops. Listed in africa-tiles.txt (from the bucket's
# tileList.txt, fetched 2026-09-26, CRLF stripped). Resumable; sha256 of every
# tile in tiles-sha256.txt.
set -uo pipefail
cd "$(dirname "$0")"
fetch() { [ -s tiles/$1.tif ] || { curl -fsSL --retry 5 -o tiles/$1.tif.part https://copernicus-dem-30m.s3.amazonaws.com/$1/$1.tif && mv tiles/$1.tif.part tiles/$1.tif; } || echo "FAILED $1"; }
export -f fetch
xargs -P 32 -I{} bash -c 'fetch {}' < africa-tiles.txt
echo "tiles present: $(ls tiles | grep -c '\.tif$') of $(wc -l < africa-tiles.txt)"
( cd tiles && sha256sum *.tif ) > tiles-sha256.txt
echo DEM FETCH DONE
