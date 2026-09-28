#!/usr/bin/env bash
# Gemini inputs for the scaling series: GRAPH.gemini32 (+ .info) beside each
# .wsg, written by wsg_to_gemini (12 bytes per directed edge, both directions,
# 32-bit ids). Written as .tmp, size-checked against the .wsg's edge count,
# then renamed; an existing complete file is kept.
#
#   sbatch scripts/frontier/convert_gemini.sbatch CAMPAIGN GRAPH[,GRAPH...]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=01:30:00
#SBATCH --job-name=acic-convert-gemini
set -uo pipefail
ROOT=${1:?campaign}
GRAPHS=${2:?GRAPH[,GRAPH...]}
G=$ROOT/graphs
echo "job=$SLURM_JOB_ID host=$(hostname) graphs=$GRAPHS"
sha256sum "$ROOT/bin/wsg_to_gemini"
FAILED=
for graph in ${GRAPHS//,/ }; do
  out=$G/$graph.gemini32
  edges=$(od -An -tu8 -j1 -N8 "$G/$graph.wsg" | tr -d ' ')
  want=$((edges * 12))
  if [ -s "$out.info" ] && [ "$(stat -c %s "$out" 2>/dev/null)" = "$want" ]; then
    echo "KEEP $graph ($want bytes)"; continue
  fi
  rm -f "$out.tmp" "$out.tmp.info"
  lfs setstripe -c 16 -S 16M "$out.tmp"
  start=$SECONDS
  OMP_NUM_THREADS=56 /usr/bin/time -v "$ROOT/bin/wsg_to_gemini" --ids 32 "$G/$graph.wsg" "$out.tmp" \
    > "$G/$graph.gemini32.log" 2>&1
  rc=$?
  got=$(stat -c %s "$out.tmp" 2>/dev/null)
  if [ $rc -eq 0 ] && [ "$got" = "$want" ] && [ -s "$out.tmp.info" ]; then
    mv "$out.tmp.info" "$out.info" && mv "$out.tmp" "$out"
    echo "DONE $graph edges=$edges bytes=$got in $((SECONDS - start)) s"
  else
    echo "FAIL $graph rc=$rc bytes=$got want=$want"; tail -5 "$G/$graph.gemini32.log"; FAILED="$FAILED $graph"
  fi
done
[ -z "$FAILED" ] && echo "CONVERT COMPLETE" || { echo "CONVERT FAILED:$FAILED"; exit 1; }
