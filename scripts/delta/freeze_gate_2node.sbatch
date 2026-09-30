#!/usr/bin/env bash
# The D6 freeze candidate (or, with PROFILES=o, the round-2 O1-O3 flags on
# the 986c0d8 binaries) through the two-node envelope gate
# (scripts/verify_2node.sh): every binary and flag profile the freeze ships,
# including the 12-byte compact64 wire the Frontier series uses, on the gate's
# generated graphs and on .wsg copies read through the reader (mode 4, with
# --reader-tile auto for every profile but the default). Each profile
# writes its logs under CAMPAIGN/logs/freeze-gate-JOB/PROFILE and the job
# fails if any profile does.
#
#   sbatch scripts/delta/freeze_gate_2node.sbatch CAMPAIGN     (from the repo root)
#   sbatch --export=ALL,PROFILES=o scripts/delta/freeze_gate_2node.sbatch CAMPAIGN
# The O profiles add graphs/gate/leafy.wsg (tools/leafy_graph.py: undirected,
# 27.5% leaves) from source 0 and from the degree-1 source 199999.
#SBATCH --account=rfp-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --exclusive
#SBATCH --mem=0
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-freeze-gate
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign}
OUT=$ROOT/logs/freeze-gate-$SLURM_JOB_ID
mkdir -p "$OUT/bin"
export SLURM_MPI_TYPE=cray_shasta
unset LCT_PMI_BACKEND
# --reader-tile refuses generated graphs, so it goes on the file-mode runs only
# (SSSP_FILE_ARGS); the generated-graph matrix gets the rest of the profile.
COMMON="--process-share auto --slack-control off --process-queue nearest --process-queue-batch 8 +old-scheduler --hub-hints auto"
# label|binary|extra args
profiles=(
  "heap-default|acic_frz_heap|"
  "heap-candidate|acic_frz_heap|$COMMON --heap-slice 8"
  "heap-plain-async|acic_frz_heap|$COMMON --heap-slice 8 --admission all"
  "c256-chunks|acic_frz_c256|$COMMON --heap-slice 64"
  "c65536-chunks|acic_frz_c65536|$COMMON --heap-slice 64"
  "heap-w64-candidate|acic_frz_heap_w64|$COMMON --heap-slice 8"
  "c256-w64-chunks|acic_frz_c256_w64|$COMMON --heap-slice 64"
)
FILE_GRAPHS="uniform:1 mesh:0 rmat:0"
if [ "${PROFILES:-freeze}" = o ]; then
  O="--leaf-prune auto --heap-drain idle"
  profiles=(
    "o-heap-default|acic_o3_heap|"
    "o-heap-leaf-idle|acic_o3_heap|$COMMON --heap-slice 8 $O"
    "o-c65536-leaf-idle|acic_o3_c65536|$COMMON --heap-slice 64 $O"
    "o-c256-leaf-idle|acic_o_c256|$COMMON --heap-slice 64 $O"
    "o1-b256-leaf-idle|acic_o1_bkt|$COMMON --heap-slice 64 --chunk-band 256 $O"
    "o1-b4096|acic_o1_bkt|$COMMON --heap-slice 64 --chunk-band 4096"
    "o1-b1-spill|acic_o1_bkt|$COMMON --heap-slice 64 --chunk-band 1"
  )
  FILE_GRAPHS="uniform:1 mesh:0 rmat:0 leafy:0 leafy:199999"
  rmdir "$OUT/bin" "$OUT"; OUT=$ROOT/logs/o-gate-$SLURM_JOB_ID; mkdir -p "$OUT/bin"
fi
echo "job=$SLURM_JOB_ID nodes=$SLURM_JOB_NODELIST"
failed=0
for p in "${profiles[@]}"; do
  IFS='|' read -r label binary extra <<< "$p"
  # verify_2node.sh kills stragglers by matching 'sssp_smp' in the command line.
  ln -sf "$ROOT/bin/$binary" "$OUT/bin/sssp_smp-$label"
  sha256sum "$ROOT/bin/$binary"
  echo "===== $label ($binary) extra: ${extra:-<none>}"
  tile="--reader-tile auto"; [ -z "$extra" ] && tile=
  if SSSP_BIN="$OUT/bin/sssp_smp-$label" SSSP_LOGS="$OUT/$label" SSSP_EXTRA_ARGS="$extra" \
       SSSP_FILE_DIR="$ROOT/graphs/gate" SSSP_FILE_ARGS="$tile" SSSP_FILE_GRAPHS="$FILE_GRAPHS" \
       bash scripts/verify_2node.sh; then
    echo "PROFILE PASSED $label"
  else
    echo "PROFILE FAILED $label"; failed=$((failed + 1))
  fi
done
[ "$failed" -eq 0 ] && echo "FREEZE 2-NODE GATE PASSED (${#profiles[@]} profiles)" || { echo "FREEZE 2-NODE GATE FAILED ($failed of ${#profiles[@]})"; exit 1; }
