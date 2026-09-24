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
for pair in road-usa-z:road mesh26-z:mesh; do
  graph=${pair%:*}
  config=$APP/benchmarks/delta-heap-coalesce-${pair#*:}-variants.json
  python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$config" \
    --rpn 16 --workers 112 --sources 2 --source-role tune --reps 3 --batch
  python3 "$APP/benchmarks/check_priority_runs.py" "$ROOT" "$SLURM_JOB_ID" \
    --config "$config" --graphs "$graph" --sources 2 --source-role tune --reps 3 \
    --output "$ROOT/logs/coalesce-audit-$SLURM_JOB_ID-$graph.json"
  python3 "$APP/benchmarks/work_cost_report.py" \
    "$ROOT/logs/AB-$graph-${SLURM_NNODES}n-$SLURM_JOB_ID" \
    > "$ROOT/logs/coalesce-work-$SLURM_JOB_ID-$graph.json"
done
python3 - "$ROOT" "$SLURM_JOB_ID" "$SLURM_NNODES" <<'PY'
from pathlib import Path
import sys
root, job, nodes = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
for graph in ['road-usa-z', 'mesh26-z']:
    logs = list((root/'logs'/f'AB-{graph}-{nodes}n-{job}').glob('*.launch.out'))
    assert len(logs) == 20, (graph, len(logs))
    for log in logs:
        text = log.read_text()
        assert 'Using the original scheduler (+old-scheduler)' in text, log
        if log.name.startswith('coalesce'):
            assert 'Heap wakeups: coalesced\n' in text, log
print('COALESCE AUDIT PASS: 80 serial-reference digests, work accounting, old scheduler')
PY

# One source, outside the timed A/B, to check whether the queue backlog fell.
TRACE=$ROOT/profile/coalesce-$SLURM_JOB_ID
mkdir -p "$TRACE"
srun -N "$SLURM_NNODES" -n $((SLURM_NNODES * 16)) --ntasks-per-node=16 \
  -c 8 --cpu-bind=none --unbuffered --kill-on-bad-exit=1 \
  bash "$APP/benchmarks/launch_acic.sh" 16 112 "$ROOT/bin/acic_coalesce_trace" \
  0 "$ROOT/graphs/road-usa-z.wsg" 1 5620086 4 0.999 0.005 \
  --result-digest --timeout 120 --process-share auto --reader-tile auto \
  --slack-control off --process-queue nearest --process-queue-batch 8 \
  --heap-slice 8 --bucket-width 131072 --hub-hints auto \
  +old-scheduler +traceoff +traceroot "$TRACE" > "$TRACE/run.out" 2>&1
python3 "$APP/benchmarks/check_onenode_digest.py" \
  "$ROOT/graphs/road-usa-z.reference.txt" 5620086 "$TRACE/run.out"
python3 "$APP/benchmarks/projections_report.py" "$TRACE/acic_coalesce_trace" \
  --pes-per-process 7 --processes-per-node 16 --jobs 8 \
  --json "$TRACE/summary.json" > "$TRACE/summary.txt"
python3 "$APP/benchmarks/heap_backlog_report.py" "$TRACE/acic_coalesce_trace" \
  --jobs 8 --require-single --json "$TRACE/heap-backlog.json"
echo "HEAP COALESCE COMPLETE $SLURM_JOB_ID"
