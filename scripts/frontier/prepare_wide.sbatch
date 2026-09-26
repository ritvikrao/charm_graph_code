#!/usr/bin/env bash
# Inputs too large for the one-node pipeline (prepare_inputs.sbatch): the
# wide synthetic grids (grid_graph) and the GLO-30 terrain series
# (terrain_graph2), each written as .wsg and Galois .gr in one pass. No
# reference here: references come from certified ACIC runs
# (terrain_reference.sbatch CAMPAIGN NAME), as for terrain-ae-z.
#
#   KIND=grid    sbatch prepare_wide.sbatch CAMPAIGN NAME DIM VERTICES MAX_WEIGHT
#   KIND=terrain sbatch prepare_wide.sbatch CAMPAIGN NAME "LAT0 LAT1 LON0 LON1" "SEED_LAT SEED_LON" [--narrow]
#
# PART=K/P writes one of P parts (P jobs can run at once); stripe the two .tmp
# files first, on a login node: lfs setstripe -c 64 -S 16M NAME.{wsg,gr}.tmp.
# After every part, FINALIZE=P (no build) joins the part logs into NAME.meta
# and NAME.sources and renames the files. Without PART the build is one job.
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-wide
set -euo pipefail
C=${1:?campaign}
NAME=${2:?name}
KIND=${KIND:-grid}
OUT=$C/graphs/$NAME
echo "job=${SLURM_JOB_ID:-none} host=$(hostname) name=$NAME kind=$KIND part=${PART:-} finalize=${FINALIZE:-}"
if [ -n "${FINALIZE:-}" ]; then
  logs=()
  for k in $(seq 0 $((FINALIZE - 1))); do logs+=("$OUT.part$k-of-$FINALIZE.log"); done
  for f in "${logs[@]}"; do grep -q '^part=' "$f" || { echo "PART MISSING $f"; exit 1; }; done
  vertices=$(grep -h -m1 -o '^vertices=[0-9]*' "${logs[@]}" | sort -u)
  arcs=$(grep -h -m1 -o 'directed_edges=[0-9]*\|arcs=[0-9]*' "${logs[0]}" | head -1 | cut -d= -f2)
  [ "$(echo "$vertices" | wc -l)" = 1 ] || { echo "PARTS DISAGREE: $vertices"; exit 1; }
  edges=$(grep -h -o 'part_edges=[0-9]*' "${logs[@]}" | cut -d= -f2 | awk '{s += $1} END {print s}')
  [ -z "$edges" ] || [ "$edges" = "$arcs" ] || { echo "PART EDGES $edges != $arcs"; exit 1; }
  max_weight=$(grep -h -o ' max_weight=[0-9]*' "${logs[@]}" | cut -d= -f2 | sort -n | tail -1)
  d=1; while [ "$d" -lt "$max_weight" ]; do d=$((d * 2)); done
  echo "$vertices arcs=$arcs max_weight=$max_weight riken_denominator=$d" > "$OUT.meta"
  grep -h -m1 '^sources ' "${logs[0]}" | sed 's/^sources //' > "$OUT.sources"
  for f in "${logs[@]}"; do [ "$(grep -h -m1 '^sources ' "$f")" = "$(grep -h -m1 '^sources ' "${logs[0]}")" ] || { echo "SOURCES DIFFER"; exit 1; }; done
  mv "$OUT.wsg.tmp" "$OUT.wsg"; mv "$OUT.gr.tmp" "$OUT.gr"; touch "$OUT.gr.done"
  ls -la "$OUT.wsg" "$OUT.gr"; echo "WIDE COMPLETE $NAME $(cat "$OUT.meta")"
  exit 0
fi
module load gcc-native/13.2 >/dev/null 2>&1
PARTARG=(); LOG=$OUT.build.log
if [ -n "${PART:-}" ]; then PARTARG=(--part "$PART"); LOG=$OUT.part${PART%/*}-of-${PART#*/}.log; fi
for f in "$OUT.wsg.tmp" "$OUT.gr.tmp"; do
  [ -e "$f" ] || { [ -z "${PART:-}" ] && lfs setstripe -c 64 -S 16M "$f"; } || { echo "stripe $f first"; exit 1; }
done
export OMP_NUM_THREADS=56 OMP_PROC_BIND=spread
case $KIND in
  grid)
    sha256sum "$C/bin/grid_graph"
    /usr/bin/time -v "$C/bin/grid_graph" "${3:?dim}" "${4:?vertices}" 1 "${5:?max weight}" 6 "$OUT.wsg.tmp" \
      --gr "$OUT.gr.tmp" "${PARTARG[@]}" > "$LOG" 2> "$LOG.err" ;;
  terrain)
    sha256sum "$C/bin/terrain_graph2"
    # shellcheck disable=SC2086
    /usr/bin/time -v "$C/bin/terrain_graph2" "$C/raw/dem/glo30/f32" ${3:?region} ${4:?seed} 6 "$OUT.wsg.tmp" \
      --arcsec 1 --gr "$OUT.gr.tmp" ${5:-} "${PARTARG[@]}" > "$LOG" 2> "$LOG.err" ;;
  *) echo "unknown KIND $KIND"; exit 2 ;;
esac || { cat "$LOG.err"; echo "WIDE FAILED $NAME"; exit 1; }
cat "$LOG"; grep -E 'Maximum resident|Elapsed' "$LOG.err"
if [ -z "${PART:-}" ]; then
  grep '^vertices=.*riken_denominator' "$LOG" | tail -1 > "$OUT.meta"
  sed -n 's/^sources //p' "$LOG" > "$OUT.sources"
  mv "$OUT.wsg.tmp" "$OUT.wsg"; mv "$OUT.gr.tmp" "$OUT.gr"; touch "$OUT.gr.done"
  ls -la "$OUT.wsg" "$OUT.gr"; echo "WIDE COMPLETE $NAME $(cat "$OUT.meta")"
else
  echo "WIDE PART $PART DONE $NAME"
fi
