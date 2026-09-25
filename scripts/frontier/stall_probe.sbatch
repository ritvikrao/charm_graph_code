#!/usr/bin/env bash
# Stall probe: how often does each arm stall on one graph, and on which
# sources? Unlike onenode_ab.py, a stalled or wrong solve is counted, not
# fatal. Diagnosis only; it selects nothing.
#
# Arms are LABEL=BINARY[:flag,flag...] (commas inside flags become spaces).
# Each launch solves SOURCES in order (repeats allowed) with --sources; arms
# rotate each launch. stderr, with CXI warnings, goes to OUT/stderr.
#
#   SOURCES=27797227,27797227,47836480 LAUNCHES=4 sbatch -N 16 \
#     scripts/frontier/stall_probe.sbatch CAMPAIGN rmat26 4 \
#     h2tls=acic_hint2_tls:--hub-hints,auto frozen=acic_slice
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=16
#SBATCH --time=00:45:00
#SBATCH --job-name=acic-stall
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
GRAPH=${2:?graph}
RPN=${3:?ranks per node}
shift 3
LAUNCHES=${LAUNCHES:-4}
SOURCES=${SOURCES:?comma-separated sources}
BASE="--process-share auto --reader-tile auto --slack-control off --process-queue nearest --process-queue-batch 8 +old-scheduler --heap-slice 8"
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
N=$SLURM_NNODES
WORKERS=56
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid FI_LOG_LEVEL=warn FI_LOG_PROV=cxi
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
OUT=$ROOT/logs/stall-$GRAPH-${N}n-$SLURM_JOB_ID
mkdir -p "$OUT"
printf 'job=%s hosts=%s sources=%s arms=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST" "$SOURCES" "$*" | tee "$OUT/job.txt"
ARMS=("$@")
for ((l = 0; l < LAUNCHES; l++)); do
  for ((k = 0; k < ${#ARMS[@]}; k++)); do
    arm=${ARMS[$(((k + l) % ${#ARMS[@]}))]}
    label=${arm%%=*}; spec=${arm#*=}; bin=${spec%%:*}; extra=""
    [ "$spec" != "$bin" ] && extra=${spec#*:} && extra=${extra//,/ }
    log=$OUT/$label-l$l.out
    ACIC_STDERR_DIR=$OUT/stderr/$label-l$l \
    srun -N "$N" -n $((N * RPN)) --ntasks-per-node "$RPN" -c $((WORKERS / RPN)) --cpu-bind=none \
      --unbuffered --kill-on-bad-exit=1 --time=00:08:00 \
      bash "$APP/benchmarks/launch_acic.sh" "$RPN" "$WORKERS" "$ROOT/bin/$bin" 0 \
      "$ROOT/graphs/$GRAPH.wsg" 1 "${SOURCES%%,*}" 4 0.999 0.005 --result-digest --timeout 300 \
      $BASE $extra --sources "$SOURCES" > "$log" 2>&1 || echo "$label l$l: exit $?"
    echo "$label l$l: $(grep -c '^SOURCE_RUN' "$log") solves, $(grep -c '^PROGRESS_STALL [0-9]* main' "$log") stall reports"
  done
done
"$PY" - "$OUT" "$ROOT/graphs/$GRAPH.reference.txt" <<'EOF' | tee "$OUT/summary.txt"
import re, sys, glob, collections, statistics
out, ref = sys.argv[1], sys.argv[2]
refs = {r.split()[0]: r.split()[2:6] for r in open(ref) if r[:1].isdigit()}
cells = collections.defaultdict(list)
for log in sorted(glob.glob(out + '/*-l*.out')):
    label = re.sub(r'-l\d+\.out$', '', log.split('/')[-1])
    parts = re.split(r'^SOURCE_RUN index=\d+ source=(\d+)\n', open(log).read(), flags=re.M)
    for source, text in zip(parts[1::2], parts[2::2]):
        d = re.search(r'VERIFY parallel digest h1=(\d+) h2=(\d+) reachable=(\d+) distance_sum=(\d+)', text)
        t = re.search(r'^Compute time: ([\d.eE+-]+)', text, re.M)
        cells[label, source].append(dict(ok=bool(d) and list(d.groups()) == refs[source],
            stall=bool(re.search(r'^PROGRESS_STALL \d+ main', text, re.M)), seconds=float(t[1]) if t else None))
for (label, source), rs in sorted(cells.items()):
    times = [r['seconds'] for r in rs if r['seconds'] and not r['stall']]
    print(f'{label:12} {source:>10} solves={len(rs):3} wrong={sum(not r["ok"] for r in rs)} '
          f'stalled={sum(r["stall"] for r in rs)} median_unstalled={statistics.median(times) if times else float("nan"):.4f} '
          f'stalled_times={[round(r["seconds"], 3) for r in rs if r["stall"] and r["seconds"]]}')
EOF
grep -h -v '^\s*$' "$OUT"/stderr/*/* 2>/dev/null | sed -E 's/[0-9]+/N/g' | sort | uniq -c | sort -rn | head -20 | tee "$OUT/stderr-summary.txt"
echo "STALL PROBE COMPLETE $SLURM_JOB_ID"
