#!/usr/bin/env bash
# One-node held-out A/B of the process queue (heap versus 256-band private
# chunks) in the debug QOS, then Wasp on the same held-out sources and node,
# then the raw-log audit. onenode_ab.py interleaves the arms of VARIANTS on
# the four held-out sources (one warmup, REPS timed repetitions, graph retained
# across sources); WASP gives each graph's tuned delta.
#
#   sbatch scripts/frontier/chunks_1n.sbatch CAMPAIGN VARIANTS.json mesh28-z:4096
#   sbatch scripts/frontier/chunks_1n.sbatch CAMPAIGN VARIANTS.json \
#       road-usa-z:32768,road-na-z:8192,road-eu-z:8192
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --qos=debug
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-chunks-1n
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
CELLS=${3:?graph:wasp_delta[,graph:wasp_delta...]}
SOURCES=${SOURCES:-4}
ROLE=${ROLE:-test}
REPS=${REPS:-3}
WASP_REPS=${WASP_REPS:-3}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
GRAPHS=
for cell in ${CELLS//,/ }; do
  graph=${cell%%:*}
  GRAPHS=${GRAPHS:+$GRAPHS,}$graph
  "$PY" "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$VARIANTS" \
    --sources "$SOURCES" --reps "$REPS" --source-role "$ROLE" --batch
  if grep -q '"work_cost": true' "$VARIANTS"; then
    "$PY" "$APP/benchmarks/work_cost_report.py" "$ROOT/logs/AB-$graph-1n-$SLURM_JOB_ID"
  fi
done
# Wasp: the same held-out sources (reference rows with this role, in order).
for cell in ${CELLS//,/ }; do
  graph=${cell%%:*} delta=${cell##*:}
  out=$ROOT/logs/wasp-1n-$graph-$SLURM_JOB_ID.txt
  awk -F'\t' -v role="$ROLE" '$2 == role' "$ROOT/graphs/$graph.reference.txt" | head -n "$SOURCES" |
  while IFS=$'\t' read -r source _ h1 h2 reachable sum _; do
    expect="h1=$h1 h2=$h2 reachable=$reachable distance_sum=$sum"
    for rep in $(seq 1 "$WASP_REPS"); do
      w=$(srun -N 1 -n 1 --ntasks-per-node 1 -c 56 --cpu-bind=cores --unbuffered --kill-on-bad-exit=1 \
          "$ROOT/bin/wasp_sssp" "$ROOT/graphs/$graph.wsg" "$source" "$delta" 2>&1 | grep -m1 '^BENCH') || true
      got=$(grep -o 'h1=[0-9]* h2=[0-9]* reachable=[0-9]* distance_sum=[0-9]*' <<< "$w" || true)
      printf '%s\t%s\t%s\t%s\t%s\n' "$graph" "$source" "$rep" \
        "$(grep -o 'solve_seconds=[0-9.]*' <<< "$w" | cut -d= -f2)" \
        "$([ -n "$got" ] && [ "$got" = "$expect" ] && echo VALID || echo INVALID)"
    done
  done | tee "$out"
done
"$PY" "$APP/benchmarks/check_priority_runs.py" "$ROOT" "$SLURM_JOB_ID" \
  --config "$VARIANTS" --graphs "$GRAPHS" --sources "$SOURCES" --source-role "$ROLE" --reps "$REPS" \
  --output "$ROOT/logs/onenode-audit-$SLURM_JOB_ID.json"
echo "CHUNKS COMPLETE $SLURM_JOB_ID"
