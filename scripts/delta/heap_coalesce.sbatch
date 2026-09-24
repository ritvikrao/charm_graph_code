#!/usr/bin/env bash
# A single intervention: suppress a heap wakeup if one is already pending.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=00:20:00
#SBATCH --job-name=acic-heap-coalesce
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND
ROAD_JOB=${RESUME_ROAD_JOB:-$SLURM_JOB_ID}
for pair in road-usa-z:road mesh26-z:mesh; do
  graph=${pair%:*}
  config=$APP/benchmarks/delta-heap-coalesce-${pair#*:}-variants.json
  DATA_JOB=$SLURM_JOB_ID
  if [ "$graph" = road-usa-z ] && [ -n "${RESUME_ROAD_JOB:-}" ]; then
    DATA_JOB=$RESUME_ROAD_JOB
  else
    python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$config" \
      --rpn 16 --workers 112 --sources 2 --source-role tune --reps 3 --batch
  fi
  python3 "$APP/benchmarks/work_cost_report.py" \
    "$ROOT/logs/AB-$graph-${SLURM_NNODES}n-$DATA_JOB" \
    > "$ROOT/logs/coalesce-work-$DATA_JOB-$graph.json"
  python3 "$APP/benchmarks/check_priority_runs.py" "$ROOT" "$DATA_JOB" \
    --config "$config" --graphs "$graph" --sources 2 --source-role tune --reps 3 \
    --rpn 16 --workers 112 --output "$ROOT/logs/coalesce-audit-$DATA_JOB-$graph.json"
done
python3 - "$ROOT" "$SLURM_JOB_ID" "$SLURM_NNODES" "$ROAD_JOB" <<'PY'
from pathlib import Path
import sys
root, job, nodes = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
for graph in ['road-usa-z', 'mesh26-z']:
    data_job = sys.argv[4] if graph == 'road-usa-z' else job
    logs = list((root/'logs'/f'AB-{graph}-{nodes}n-{data_job}').glob('*.launch.out'))
    assert len(logs) == 20, (graph, len(logs))
    for log in logs:
        text = log.read_text()
        assert 'Using the original scheduler (+old-scheduler)' in text, log
        if log.name.startswith('coalesce'):
            assert 'Heap wakeups: coalesced\n' in text, log
print('COALESCE AUDIT PASS: 80 serial-reference digests, work accounting, old scheduler')
PY

# Matched traces on this allocation: cross-allocation absolute times differ.
TRACE_ROOT=$ROOT/profile/coalesce-$SLURM_JOB_ID
mkdir -p "$TRACE_ROOT"
sha256sum "$ROOT/bin/acic_round_trace" "$ROOT/bin/acic_coalesce_trace" \
  > "$TRACE_ROOT/binaries.sha256"
for ARM in base coalesce; do
TRACE=$TRACE_ROOT/$ARM
mkdir -p "$TRACE"
BIN=acic_round_trace
CHECK=()
if [ "$ARM" = coalesce ]; then
  BIN=acic_coalesce_trace
  CHECK=(--require-single)
fi
srun -N "$SLURM_NNODES" -n $((SLURM_NNODES * 16)) --ntasks-per-node=16 \
  -c 8 --cpu-bind=none --unbuffered --kill-on-bad-exit=1 \
  bash "$APP/benchmarks/launch_acic.sh" 16 112 "$ROOT/bin/$BIN" \
  0 "$ROOT/graphs/road-usa-z.wsg" 1 5620086 4 0.999 0.005 \
  --result-digest --timeout 120 --process-share auto --reader-tile auto \
  --slack-control off --process-queue nearest --process-queue-batch 8 \
  --heap-slice 8 --bucket-width 131072 --hub-hints auto \
  +old-scheduler +traceoff +traceroot "$TRACE" > "$TRACE/run.out" 2>&1
python3 "$APP/benchmarks/check_onenode_digest.py" \
  "$ROOT/graphs/road-usa-z.reference.txt" 5620086 "$TRACE/run.out"
python3 "$APP/benchmarks/projections_report.py" "$TRACE/$BIN" \
  --pes-per-process 7 --processes-per-node 16 --jobs 8 \
  --json "$TRACE/summary.json" > "$TRACE/summary.txt"
python3 "$APP/benchmarks/heap_backlog_report.py" "$TRACE/$BIN" \
  --jobs 8 "${CHECK[@]}" --json "$TRACE/heap-backlog.json"
done
echo "HEAP COALESCE COMPLETE $SLURM_JOB_ID"
