#!/usr/bin/env bash
# Delta one-node IPDPS inputs (plan items D1 and D3). Same preparation as
# scripts/frontier/prepare_inputs.sbatch, restricted to the graphs the Delta
# experiments need and without the Galois .gr copies (Gluon runs on Frontier):
#
#   grid3-30-z     1024^3 Morton 3-D grid, six neighbours, weights [1, 1000]  (D1)
#   mesh28-w10-z   mesh28-z's topology and ids, weights [1, 10]               (D3)
#   mesh30-z       2^30-vertex Morton 2-D mesh, weights [1, 1000]              (D7)
#   mesh28-w64k-z  the same with weights [1, 65536]                           (D3, optional)
#   terrain30-c-z  GLO-30 crop, lat [3, 9) x lon [24, 31), seed 6N 27E: nested
#                  in terrain30-s-z, 42 tiles, one node's share of the Frontier
#                  weak-scaling series (505-541M vertices)                     (D1)
#                  Tiles first: scripts/delta/fetch_terrain_crop.sh CAMPAIGN
#
# Graphs already built by an earlier Delta campaign (mesh28-z, road-usa-z) are
# linked in by scripts/delta/ipdps_link_inputs.sh rather than regenerated.
#
#   sbatch scripts/delta/ipdps_prepare.sbatch CAMPAIGN [names] [parallel]
#
# STEP=generate writes only the .wsg; STEP=reference writes only the
# independent Dijkstra reference. Both steps resume: an artifact that already
# exists is left alone, so a job that hits the one-hour interactive limit can
# simply be resubmitted.
#SBATCH --account=rfp-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-ipdps-inputs
set -uo pipefail
ulimit -c 0
CAMPAIGN=${1:?campaign directory}
NAMES=${2:-grid3-30-z,mesh28-w10-z}
PARALLEL=${3:-1}
STEP=${STEP:-all}
BIN=$CAMPAIGN/bin
DATA=$CAMPAIGN/graphs
mkdir -p "$DATA"
export OMP_NUM_THREADS=${OMP_NUM_THREADS:-128} OMP_PROC_BIND=${OMP_PROC_BIND:-spread}
export BIN DATA STEP
echo "job=${SLURM_JOB_ID:-none} host=$(hostname) names=$NAMES step=$STEP threads=$OMP_NUM_THREADS"

gridz() {     # name dim vertices seed max_weight
  /usr/bin/time -v "$BIN/grid_graph" "$2" "$3" "$4" "$5" 6 "$DATA/$1.wsg.tmp" \
    > "$DATA/$1.build.log" 2> "$DATA/$1.build.err" &&
    grep -m1 '^vertices=.* arcs=' "$DATA/$1.build.log" > "$DATA/$1.meta.tmp"
}
terrainz() {  # name lat0 lat1 lon0 lon1 seed_lat seed_lon
  /usr/bin/time -v "$BIN/terrain_graph2" "$CAMPAIGN/raw/dem/glo30/f32" "$2" "$3" "$4" "$5" "$6" "$7" 6 \
    "$DATA/$1.wsg.tmp" --arcsec 1 --narrow > "$DATA/$1.build.log" 2> "$DATA/$1.build.err" &&
    grep -m1 '^vertices=.* arcs=' "$DATA/$1.build.log" > "$DATA/$1.meta.tmp"
}
prepare_one() {
  local name=$1 base=$DATA/$1
  if [ "$STEP" != reference ] && { [ ! -s "$base.wsg" ] || [ ! -s "$base.meta" ]; }; then
    case $name in
      grid3-30-z)    gridz "$name" 3 1073741824 1 1000 ;;
      mesh30-z)      gridz "$name" 2 1073741824 1 1000 ;;
      mesh28-w10-z)  gridz "$name" 2 268435456 1 10 ;;
      mesh28-w64k-z) gridz "$name" 2 268435456 1 65536 ;;
      terrain30-c-z) terrainz "$name" 3 9 24 31 6 27 ;;
      *) echo "UNKNOWN $name"; return 1 ;;
    esac || { tail -5 "$base.build.err"; echo "PREPARE FAILED $name"; return 1; }
    mv "$base.wsg.tmp" "$base.wsg" && mv "$base.meta.tmp" "$base.meta"
    grep -E 'Maximum resident|Elapsed' "$base.build.err"
    echo "GENERATED $name $(cat "$base.meta")"
  fi
  # Six sources: two tuning and four held out, drawn exactly as every other
  # campaign draws them. mesh28-w64k-z's distances exceed binary32, so its
  # reference drops the RIKEN range guard (RIKEN runs on Frontier only).
  local range=
  # Terrain walking times are not bounded by 2^24 either (7M+ deciseconds
  # across the crop).
  case $name in mesh28-w64k-z|terrain30-c-z) range=any-range ;; esac
  if [ "$STEP" != generate ] && [ ! -s "$base.reference.txt" ]; then
    /usr/bin/time -v "$BIN/reference_gap" "$base.wsg" 6 $range \
      > "$base.reference.tmp" 2> "$base.reference.log" &&
      mv "$base.reference.tmp" "$base.reference.txt" ||
      { tail -5 "$base.reference.log"; echo "REFERENCE FAILED $name"; return 1; }
    grep -E 'Maximum resident|Elapsed' "$base.reference.log"
    echo "REFERENCED $name"
  fi
  echo "PREPARED $name $(date -Is)"
}
export CAMPAIGN
export -f gridz terrainz prepare_one

printf '%s\n' ${NAMES//,/ } | xargs -P "$PARALLEL" -I{} bash -c 'prepare_one {}'

for n in ${NAMES//,/ }; do
  [ -s "$DATA/$n.wsg" ] || continue
  grep -q " $n.wsg\$" "$DATA/sha256.txt" 2>/dev/null && continue
  ( cd "$DATA" && sha256sum "$n.wsg" ) >> "$DATA/sha256.txt"
done
for n in ${NAMES//,/ }; do printf '%-14s %s\n' "$n" "$(cat "$DATA/$n.meta" 2>/dev/null)"; done
printf 'INPUTS COMPLETE\n'
