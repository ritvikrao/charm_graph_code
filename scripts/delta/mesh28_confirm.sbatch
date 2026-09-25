#!/usr/bin/env bash
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-mesh28-confirm
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_PROF_TIMER ACIC_PROF_OFF ACIC_PROF_DIR
python3 "$APP/benchmarks/mesh28_opt_gate.py" "$ROOT" acic_chunks_256 acic_chunks_256_cost --heap-slice 64
python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" mesh28-z "$ROOT/configs/heldout.json" --rpn 16 --workers 112 --sources 4 --source-role test --reps 3
python3 "$APP/benchmarks/mesh28_ab_report.py" "$ROOT/logs/AB-mesh28-z-1n-$SLURM_JOB_ID" > "$ROOT/logs/heldout-audit-$SLURM_JOB_ID.txt"
python3 "$APP/benchmarks/mesh28_wasp_fixed.py" "$ROOT"
python3 "$APP/benchmarks/mesh28_work_probe.py" "$ROOT" "$ROOT/configs/work.json"
python3 "$APP/benchmarks/mesh28_pc_probe.py" "$ROOT" "$ROOT/configs/pc.json"
python3 "$APP/benchmarks/mesh28_opt_trace.py" "$ROOT" "$ROOT/configs/selected.json"
echo "CONFIRMATION COMPLETE"
