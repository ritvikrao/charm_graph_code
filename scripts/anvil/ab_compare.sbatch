#!/usr/bin/env bash
# Interleaved A/B of ACIC binaries (and runtime flags) on one configuration,
# for the per-edge cost work in step 7.6j. Each round runs every variant once,
# in a rotated order, so drift in the allocation lands on all of them alike.
#
# A variant is LABEL=BINARY or LABEL=BINARY:FLAGS, where BINARY is a name under
# ROOT/bin and FLAGS (commas for spaces) are appended to the solver's command
# line, except tokens of the form NAME=VALUE with an upper-case NAME, which are
# set in the launch's environment instead (LD_PRELOAD, GLIBC_TUNABLES, ...).
# A PAPI build counts without sampling and reports instructions per edge.
#
#   VARIANTS="base=acic o1=acic_o1 o1lb=acic_o1:+LBOff" ROUNDS=3 \
#     sbatch -N 2 scripts/anvil/ab_compare.sbatch \
#       /anvil/scratch/$USER/acic/campaign rmat25 23077392 8
#
# The summary gives the median solve time per variant and hardware ns per graph
# edge: PEs x seconds / edges, the unit the 7.6i comparison was made in.
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-ab
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign root}
GRAPH=${2:?graph}
SOURCE=${3:?source}
RPN=${4:-8}
WORKERS=${WORKERS:-120}  # per node; 112 for 16 x 7
VARIANTS=${VARIANTS:?set VARIANTS}
ROUNDS=${ROUNDS:-3}
module load gcc/11.2.0 openmpi/4.0.6 libfabric/1.12.0 numactl/2.0.14 hwloc/1.11.13 python/3.9.5 papi/6.0.0.1 >/dev/null 2>&1
export SLURM_MPI_TYPE=pmi2 PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid \
       NO_AFFINITY=1 OMP_PROC_BIND=close OMP_PLACES=cores
export LIBPFM_FORCE_PMU=amd64_fam17h_zen2
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
N=$SLURM_NNODES
OUT=$ROOT/ab/$GRAPH-${N}n-${RPN}x$((WORKERS / RPN))-$SLURM_JOB_ID
mkdir -p "$OUT"
printf 'job=%s hosts=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST"
read -r -a VARS <<< "$VARIANTS"
for v in "${VARS[@]}"; do
  bin=${v#*=}; bin=${bin%%:*}
  sha256sum "$ROOT/bin/$bin"
done
REF=$(grep -E "^$SOURCE\b" "$ROOT/graphs/$GRAPH.reference.txt" | awk '{print $3}')

launch() {  # launch BINARY LABEL [charm flags...]
  local bin=$1 label=$2; shift 2
  local try
  for try in 1 2; do
    srun -N "$N" -n $((N * RPN)) --ntasks-per-node "$RPN" -c $((128 / RPN)) \
         --cpu-bind=none --unbuffered --kill-on-bad-exit=1 \
         bash "$APP/benchmarks/launch_acic.sh" "$RPN" "$WORKERS" "$bin" \
         0 "$ROOT/graphs/$GRAPH.wsg" 1 "$SOURCE" 4 0.999 0.005 \
         --result-digest --timeout 300 "$@" > "$OUT/$label.out" 2>&1 && return 0
    grep -q 'Task launch for' "$OUT/$label.out" && [ $try -eq 1 ] || return 1
    echo "$label: step launch failed, retrying"
  done
}

launch "$ROOT/bin/${VARS[0]#*=}" warmup || true
echo "variant round seconds ns_per_edge insts_per_edge digest" > "$OUT/runs.txt"
nv=${#VARS[@]}
for ((r = 0; r < ROUNDS; r++)); do
  for ((k = 0; k < nv; k++)); do
    v=${VARS[$(((k + r) % nv))]}
    label=${v%%=*}
    spec=${v#*=}
    bin=${spec%%:*}
    flags=""
    [[ $spec == *:* ]] && flags=${spec#*:}
    args=() envs=()
    IFS=',' read -r -a toks <<< "$flags"
    for t in "${toks[@]}"; do
      if [[ $t =~ ^[A-Z_][A-Z0-9_]*= ]]; then envs+=("$t"); else args+=("$t"); fi
    done
    if ! (for e in "${envs[@]}"; do export "$e"; done
          launch "$ROOT/bin/$bin" "$label-r$r" "${args[@]}"); then
      echo "$label r$r FAILED"
      continue
    fi
    f="$OUT/$label-r$r.out"
    secs=$(awk '/^Compute time/{print $3}' "$f")
    edges=$(awk '/^Actual edges/{print $3}' "$f")
    ins=$(awk '/^Insts per edge/{print $4}' "$f")
    h1=$(grep -oE 'VERIFY parallel digest h1=[0-9]+' "$f" | cut -d= -f2)
    ok=bad; [ "$h1" = "$REF" ] && ok=ok
    nspe=$(awk -v s="$secs" -v e="$edges" -v p=$((N * WORKERS)) 'BEGIN{printf "%.1f", p*s*1e9/e}')
    echo "$label $r $secs $nspe ${ins:--} $ok" | tee -a "$OUT/runs.txt"
  done
done

python3 - "$OUT/runs.txt" <<'EOF'
import sys, statistics, collections
rows = [l.split() for l in open(sys.argv[1]).read().splitlines()[1:]]
by = collections.OrderedDict()
for v, r, s, ns, ins, ok in rows:
    by.setdefault(v, []).append((float(s), float(ns), ins, ok))
base = None
print('\n| variant | runs | median s | min s | ns/edge (median) | vs first | insts/edge | digests |')
print('|---|---:|---:|---:|---:|---:|---:|---|')
for v, xs in by.items():
    med = statistics.median(x[0] for x in xs)
    ns = statistics.median(x[1] for x in xs)
    base = base or med
    ins = [x[2] for x in xs if x[2] != '-']
    print('| %s | %d | %.3f | %.3f | %.0f | %.2fx | %s | %s |' % (
        v, len(xs), med, min(x[0] for x in xs), ns, base / med,
        ins[0] if ins else '', ','.join(sorted(set(x[3] for x in xs)))))
EOF
echo "output in $OUT"
