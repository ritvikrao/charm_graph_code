#!/usr/bin/env bash
# Compare repeated-source batches against fresh launches and serial Dijkstra.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=00:20:00
#SBATCH --job-name=acic-batch-verify
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
LABEL=${2:-acic_batch}
OUT=$ROOT/logs/$LABEL-batch-$SLURM_JOB_ID
mkdir -p "$OUT"
export LCT_PMI_BACKEND=local PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
for mode in 1 2 3; do
  for control in reduction node; do
    for share in off on; do
      log=$OUT/m$mode-$control-$share.out
      "$ROOT/bin/$LABEL" 16384 262144 1 0 "$mode" 0.999 0.005 \
        --sources 0,777,0 --verify --timeout 90 --diag "${log%.out}" \
        --control "$control" --process-share "$share" --lazy-heavy off \
        +ppn 4 > "$log" 2>&1
      [ "$(grep -c '^VERIFY PASS' "$log")" = 3 ]
      if grep -qE '^STALL_RESCUE|^CONSERVATION VIOLATED|^PROGRESS_STALL' "$log"; then exit 1; fi
      python3 - "$log" <<'PY'
import re, sys
text = open(sys.argv[1]).read()
digests = re.findall(r'^VERIFY parallel digest (.*)$', text, re.M)
assert len(digests) == 3 and digests[0] == digests[2], digests
assert len(re.findall(r'^SOURCE_RUN ', text, re.M)) == 3
PY
    done
  done
done
# Coarsening, overflow extensions, and old-generation tokens must all reset.
for flags in '--bucket-width 1 --range-extend on' '--lazy-heavy on'; do
  read -r -a args <<< "$flags"
  log=$OUT/extra-${args[0]#--}.out
  "$ROOT/bin/$LABEL" 40000 0 1 0 2 0.999 0.005 --sources 0,5050,0 \
    --verify --timeout 90 "${args[@]}" +ppn 4 > "$log" 2>&1
  [ "$(grep -c '^VERIFY PASS' "$log")" = 3 ]
done
echo "BATCH VERIFY PASS $OUT"
