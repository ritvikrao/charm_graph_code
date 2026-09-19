#!/usr/bin/env bash
# sbatch -N {1,2,8} onenode_diag.sbatch CAMPAIGN [comma-separated graphs]
# Run native orders at one node as well as the -z graphs.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-D0
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign directory}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
GRAPHS=${2:-road-usa-z,mesh24-z,mesh26-z}
BINARY=${3:-acic_ipdps_diag}
if [ "$#" -ge 3 ]; then shift 3; else shift "$#"; fi
FLAGS=("$@")
RPN=${RPN:-8}
WORKERS=${WORKERS:-120}
N=$SLURM_NNODES
OUT=$ROOT/logs/D0-${N}n-$SLURM_JOB_ID
mkdir -p "$OUT"
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
export SLURM_MPI_TYPE=cray_shasta
unset LCT_PMI_BACKEND
printf 'job=%s nodes=%s workers=%s rpn=%s hosts=%s\n' "$SLURM_JOB_ID" "$N" "$WORKERS" "$RPN" "$SLURM_JOB_NODELIST"
sha256sum "$ROOT/bin/$BINARY"
IFS=, read -r -a graphs <<< "$GRAPHS"
for graph in "${graphs[@]}"; do
  source=$(awk '$2=="tune" {print $1; exit}' "$ROOT/graphs/$graph.reference.txt")
  srun -N "$N" -n $((N * RPN)) --ntasks-per-node="$RPN" -c $((128 / RPN)) \
    --cpu-bind=none --unbuffered --kill-on-bad-exit=1 \
    bash "$APP/benchmarks/launch_acic.sh" "$RPN" "$WORKERS" "$ROOT/bin/$BINARY" \
    0 "$ROOT/graphs/$graph.wsg" 1 "$source" 4 0.999 0.005 \
    --result-digest --timeout 300 --diag "$OUT/$graph" "${FLAGS[@]}" > "$OUT/$graph.out" 2>&1
  python3 "$APP/benchmarks/check_onenode_digest.py" "$ROOT/graphs/$graph.reference.txt" "$source" "$OUT/$graph.out"
done
python3 "$APP/benchmarks/onenode_report.py" "$OUT" | tee "$OUT/report.md"
