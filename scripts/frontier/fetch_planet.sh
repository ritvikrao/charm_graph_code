#!/usr/bin/env bash
# The OSM planet snapshot of 2025-12-29 (the weekly planet closest to the
# 2026-01-01 Geofabrik extracts), fetched in 8 byte ranges in parallel and
# checked against planet.openstreetmap.org's md5.
set -euo pipefail
cd "$(dirname "$0")"
F=planet-251229.osm.pbf
U=https://osm-planet-us-west-2.s3.dualstack.us-west-2.amazonaws.com/planet/pbf/2025/$F
curl -fsSL -o $F.md5 $U.md5
SIZE=$(curl -fsSI $U | tr -d '\r' | sed -n 's/^[Cc]ontent-[Ll]ength: //p')
P=8; CHUNK=$(( (SIZE + P - 1) / P ))
for i in $(seq 0 $((P-1))); do
  a=$((i*CHUNK)); b=$(( (i+1)*CHUNK - 1 )); [ $b -ge $SIZE ] && b=$((SIZE-1))
  ( for try in 1 2 3 4 5; do
      have=$(stat -c %s $F.part$i 2>/dev/null || echo 0)
      [ $((a+have)) -gt $b ] && break
      curl -fsS -r $((a+have))-$b $U >> $F.part$i && break
      sleep 10
    done ) &
done
wait
for i in $(seq 0 $((P-1))); do cat $F.part$i; done > $F.tmp
[ "$(stat -c %s $F.tmp)" = "$SIZE" ] || { echo "SIZE MISMATCH"; exit 1; }
mv $F.tmp $F && rm -f $F.part*
md5sum -c $F.md5
sha256sum $F >> download-sha256.txt
echo PLANET FETCH DONE
