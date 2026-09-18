#!/usr/bin/env bash
# Step 8a: attribute ACIC's growth in work per edge with node count.
#
#   PARTS="diag riken arms" sbatch -N 8 scripts/anvil/step8a.sbatch \
#     /anvil/scratch/$USER/acic/campaign
#
# diag   one run per graph of the 8a counter build (DIAG_BIN, default
#        acic_diag8: STEP8A lines, rounds.csv) at ACIC's 7.6o layout, after one
#        warmup, with DIAG_FLAGS (commas for spaces).
# riken  one run per graph of RIKEN with its verbose counters (edge relaxations
#        per phase), at its 7.6o setting.
# arms   the suspect arms on the plain build, interleaved over ROUNDS rounds,
#        every run with --diag so each has a rounds.csv. An arm is
#        LABEL:BINARY:TRAM:HEAP:FLAGS -- TRAM and HEAP are the solver's two
#        positional percentiles in that order (0.999 and 0.005 by default),
#        FLAGS comma-separated. A BINARY of riken_sssp runs RIKEN at the
#        graph's delta (16 ranks x 7 threads per node), TRAM and HEAP unused.
# GRAPHS may name a graph more than once, with different sources; labels then
# carry the source (arm-GRAPH-SOURCE-LABEL-rN).
#
# GRAPHS is "graph:source:ranks_per_node:riken_delta ...", defaulting to the
# first 7.6o test source of rmat25 and orkut.
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --job-name=step8a
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign root}
PARTS=${PARTS:-diag riken arms}
ROUNDS=${ROUNDS:-3}
GRAPHS=${GRAPHS:-"rmat25:26007212:16:16 orkut:2549343:8:64"}
ARMS=${ARMS:-"base:acic:0.999:0.005: tram90:acic:0.9:0.005: tram50:acic:0.5:0.005: buf512:acic:0.999:0.005:--bufsize,512 buf2048:acic:0.999:0.005:--bufsize,2048 target32:acic:0.999:0.005:--bucket-target,32 delay05:acic:0.999:0.005:--round-delay,0.5"}
DIAG_BIN=${DIAG_BIN:-acic_diag8}
DIAG_FLAGS=${DIAG_FLAGS:-}
IFS=, read -r -a diag_flags <<< "$DIAG_FLAGS"
module load gcc/11.2.0 openmpi/4.0.6 libfabric/1.12.0 numactl/2.0.14 hwloc/1.11.13 python/3.9.5 >/dev/null 2>&1
export SLURM_MPI_TYPE=pmi2 PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid \
       NO_AFFINITY=1 OMP_PROC_BIND=close OMP_PLACES=cores
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
N=$SLURM_NNODES
OUT=$ROOT/step8a/${N}n-$SLURM_JOB_ID
mkdir -p "$OUT"
printf 'job=%s hosts=%s parts=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST" "$PARTS"
for b in $(printf '%s\n' $ARMS | cut -d: -f2 | sort -u) "$DIAG_BIN" riken_sssp_verbose; do
  sha256sum "$ROOT/bin/$b"
done

acic() {  # acic BINARY LABEL GRAPH SOURCE RPN TRAM HEAP [flags...]
  local bin=$1 label=$2 graph=$3 source=$4 rpn=$5 tram=$6 heap=$7; shift 7
  local workers=$(( rpn == 16 ? 112 : 120 )) try
  for try in 1 2; do
    srun -N "$N" -n $((N * rpn)) --ntasks-per-node "$rpn" -c $((128 / rpn)) \
         --cpu-bind=none --unbuffered --kill-on-bad-exit=1 \
         bash "$APP/benchmarks/launch_acic.sh" "$rpn" "$workers" "$ROOT/bin/$bin" \
         0 "$ROOT/graphs/$graph.wsg" 1 "$source" 4 "$tram" "$heap" \
         --result-digest --timeout 300 --diag "$OUT/$label" "$@" > "$OUT/$label.out" 2>&1 && return 0
    grep -q 'Task launch for\|Communication connection failure\|Duplicate job id' "$OUT/$label.out" \
      && [ $try -eq 1 ] || return 1
    echo "$label: step launch failed, retrying"
  done
}

check() {  # check LABEL GRAPH SOURCE -> "seconds ok|bad"
  local f=$OUT/$1.out ref h1 secs
  ref=$(grep -E "^$3\b" "$ROOT/graphs/$2.reference.txt" | awk '{print $3}')
  h1=$(grep -oE 'VERIFY parallel digest h1=[0-9]+' "$f" | cut -d= -f2)
  secs=$(awk '/^Compute time/{print $3}' "$f")
  if [ -z "$h1" ]; then  # RIKEN's BENCH line
    h1=$(grep -oE '^BENCH .*' "$f" | grep -oE 'h1=[0-9]+' | cut -d= -f2)
    secs=$(grep -oE ' solve_seconds=[0-9.e-]+' "$f" | cut -d= -f2)
  fi
  printf '%s %s' "$secs" "$([ "$h1" = "$ref" ] && echo ok || echo bad)"
}

riken() {  # riken BINARY LABEL GRAPH SOURCE DELTA
  OMP_NUM_THREADS=7 srun -N "$N" -n $((N * 16)) --ntasks-per-node 16 -c 8 --cpu-bind=cores \
    --unbuffered --kill-on-bad-exit=1 "$ROOT/bin/$1" \
    "$ROOT/graphs/$3.wsg" "$4" "$5" 1024 0 > "$OUT/$2.out" 2>&1
}

for spec in $GRAPHS; do
  IFS=: read -r graph source rpn delta <<< "$spec"
  if [[ " $PARTS " == *" diag "* ]]; then
    acic "$DIAG_BIN" "diag-$graph-warm" "$graph" "$source" "$rpn" 0.999 0.005 "${diag_flags[@]}" || true
    acic "$DIAG_BIN" "diag-$graph" "$graph" "$source" "$rpn" 0.999 0.005 "${diag_flags[@]}" \
      && echo "diag $graph $(check "diag-$graph" "$graph" "$source")" | tee -a "$OUT/summary.txt"
    grep -h '^STEP8A' "$OUT/diag-$graph.out"
  fi
  if [[ " $PARTS " == *" riken "* ]]; then
    OMP_NUM_THREADS=7 srun -N "$N" -n $((N * 16)) --ntasks-per-node 16 -c 8 --cpu-bind=cores \
      --unbuffered --kill-on-bad-exit=1 "$ROOT/bin/riken_sssp_verbose" \
      "$ROOT/graphs/$graph.wsg" "$source" "$delta" 1024 0 > "$OUT/riken-$graph.out" 2>&1
    echo "riken $graph rc=$? $(grep -E 'RELAX_SENT|Time of SSSP|Bellman-Ford iter' "$OUT/riken-$graph.out" | tr '\n' ' ')" \
      | tee -a "$OUT/summary.txt"
  fi
done

if [[ " $PARTS " == *" arms "* ]]; then
  read -r -a arms <<< "$ARMS"
  na=${#arms[@]}
  echo "graph arm round seconds digest" > "$OUT/arms.txt"
  for spec in $GRAPHS; do
    IFS=: read -r graph source rpn delta <<< "$spec"
    first=${arms[0]}; IFS=: read -r _ wbin _ <<< "$first"
    [[ $wbin == riken* ]] || acic "$wbin" "arm-$graph-$source-warm" "$graph" "$source" "$rpn" 0.999 0.005 || true
    tag=$graph
    [ "$(printf '%s\n' $GRAPHS | grep -c "^$graph:")" -gt 1 ] && tag=$graph-$source
    for ((r = 0; r < ROUNDS; r++)); do
      for ((k = 0; k < na; k++)); do
        IFS=: read -r label bin tram heap flags <<< "${arms[$(((k + r) % na))]}"
        IFS=, read -r -a extra <<< "$flags"
        name="arm-$tag-$label-r$r"
        if [[ $bin == riken* ]]; then
          riken "$bin" "$name" "$graph" "$source" "$delta"
        else
          acic "$bin" "$name" "$graph" "$source" "$rpn" "$tram" "$heap" "${extra[@]}"
        fi
        if [ $? -eq 0 ]; then
          echo "$graph $source $label $r $(check "$name" "$graph" "$source")" | tee -a "$OUT/arms.txt"
        else
          echo "$graph $source $label $r FAILED"
        fi
      done
    done
  done
fi
echo "output in $OUT"
