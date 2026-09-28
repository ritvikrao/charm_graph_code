#!/usr/bin/env bash
# One-node reference (reference_gap, 6 sources) for an input that fits on one
# node, checked against the sources the builder chose. Replaces the ad-hoc
# `sbatch --wrap` of job 5551486, which ran under /bin/sh and died on `<(...)`.
#
#   sbatch scripts/frontier/reference_gap.sbatch CAMPAIGN NAME [RANGE]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --qos=debug
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-reference-gap
set -euo pipefail
C=${1:?campaign}
NAME=${2:?graph name}
RANGE=${3:-any-range}
G=$C/graphs/$NAME
echo "job=$SLURM_JOB_ID host=$(hostname) name=$NAME range=$RANGE"
sha256sum "$C/bin/reference_gap"
OMP_NUM_THREADS=56 /usr/bin/time -v "$C/bin/reference_gap" "$G.wsg" 6 "$RANGE" \
  > "$G.reference.tmp" 2> "$G.reference.log" || { tail -20 "$G.reference.log"; echo "REFERENCE FAILED"; exit 1; }
grep -E 'Maximum resident|Elapsed' "$G.reference.log"
cat "$G.reference.tmp"
diff <(awk 'NR > 2 && $1 ~ /^[0-9]+$/ {print $1}' "$G.reference.tmp" | paste -sd' ') "$G.sources" \
  || { echo "SOURCES DIFFER FROM BUILD"; exit 1; }
mv "$G.reference.tmp" "$G.reference.txt"
echo "REFERENCE WRITTEN $G.reference.txt (sources match build)"
