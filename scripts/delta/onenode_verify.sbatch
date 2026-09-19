#!/usr/bin/env bash
# Full existing gate plus D0 conservation, provenance, and round accounting.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-gap-verify
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign directory}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export LCT_PMI_BACKEND=local PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
OUT=$ROOT/logs/verify-$SLURM_JOB_ID
mkdir -p "$OUT"
cd "$APP"
bash scripts/verify.sh > "$OUT/gate.out" 2>&1
for mode in 1 2 3; do
  for ppn in 1 4; do
    prefix=$OUT/m$mode-p$ppn
    "$ROOT/bin/acic_ipdps_diag" 16384 262144 1 0 "$mode" 0.999 0.005 \
      --verify --diag "$prefix" --timeout 60 +ppn "$ppn" > "$prefix.out" 2>&1
    grep -q '^VERIFY PASS' "$prefix.out"
    grep -q '^DIAG histogram buckets not back to zero: 0' "$prefix.out"
    grep -q '^DIAG admitted-count drift: 0' "$prefix.out"
  done
done
python3 "$APP/benchmarks/onenode_report.py" "$OUT" > "$OUT/report.md"
echo "D0 VERIFY PASS $OUT"
