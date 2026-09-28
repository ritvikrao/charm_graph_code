#!/usr/bin/env bash
# Frontier: do the Gemini and HavoqGT baselines launch here and return the
# reference answer? Two reference sources per graph (the first "tune" and the
# first "test" row), each BENCH digest compared with that row -- the check
# run.py applies -- printing PASS/FAIL per source.
#
#  Gemini (gemini_sssp, 32-bit ids): one launch per layout solves both
#    sources; inputs from wsg_to_gemini (converted here when missing, into
#    campaign/graphs-baseline-tmp). Layouts: 1 node x 1 rank x 56 threads
#    (4 NUMA sockets x 14, Gemini's own design: one process per node),
#    1 x 4 x 14 (a rank per NUMA domain), 2 x 1 x 56, 2 x 8 x 7.
#    MPICH_MAX_THREAD_SAFETY=multiple is required (Gemini calls MPI from
#    helper threads; the binary aborts without it).
#  HavoqGT (havoqgt_ingest + havoqgt_sssp): MPI only, one rank per core,
#    1 x 56 and 2 x 56. The graph store is partitioned for the ingest's rank
#    layout, so each layout ingests into node-local /dev/shm first and the
#    store is removed afterwards.
#  64-bit ids (when STRIDED=1): mesh20 with ids multiplied by 4200
#    (test64/mesh20-s4200.wsg from gluon64_test.sbatch; 4,404,019,200 ids,
#    mostly isolated), sources 737605 and 1040001 (x 4200; the second lands
#    above 2^32). Expected digests are job 5545185's, where acic_w64m
#    (certified) and gluon_sssp64 agreed and reachable/distance_sum matched
#    scipy on mesh20. gemini_sssp64 runs 2 x 1 x 56 (its O(|V|) per-rank
#    arrays favour few ranks: ~95 GB peak per rank here); HavoqGT 2 x 56
#    (~90 GB of store per node in /dev/shm).
#
#   sbatch scripts/frontier/smoke_gemini_havoqgt.sbatch CAMPAIGN [graphs] [STRIDED]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=2
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-gemini-havoqgt
set -uo pipefail
ulimit -c 0
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
CAMPAIGN=${1:?campaign directory}
GRAPHS=${2:-road-ny,mesh20,mesh24-z}
STRIDED=${3:-1}
BIN=$CAMPAIGN/bin
DATA=$CAMPAIGN/graphs
TMP=$CAMPAIGN/graphs-baseline-tmp
LOG=$CAMPAIGN/logs/smoke-gemini-havoqgt-$SLURM_JOB_ID
SHM=/dev/shm/$USER-havoqgt-$SLURM_JOB_ID
mkdir -p "$TMP" "$LOG"
# The harness environment (run.py), plus what Gemini needs.
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid NO_AFFINITY=1
export OMP_PROC_BIND=close OMP_PLACES=cores
export MPICH_MAX_THREAD_SAFETY=multiple MPICH_OFI_NIC_POLICY=ROUND-ROBIN
NN=$SLURM_NNODES
fail=0
echo "job=$SLURM_JOB_ID hosts=$SLURM_JOB_NODELIST log=$LOG"
sha256sum "$BIN"/{gemini_sssp,gemini_sssp64,wsg_to_gemini,havoqgt_sssp,havoqgt_ingest}
ldd "$BIN/gemini_sssp" "$BIN/havoqgt_sssp" | grep -E 'mpi|fabric|boost|numa|sci' | sort -u
srun -N 1 -n 1 --overlap bash -c 'numactl -H | head -3; grep Cpus_allowed_list /proc/self/status; df -h /dev/shm | tail -1; cat /proc/sys/vm/overcommit_memory'

cleanup() { srun -N "$NN" --ntasks-per-node=1 --overlap rm -rf "$SHM" >/dev/null 2>&1; }
trap cleanup EXIT

want() {  # graph source -> "h1=.. h2=.. reachable=.. distance_sum=.."
  if [ "$1" = strided ]; then
    case $2 in
      3097941000) echo "h1=4792096762574748595 h2=11172231270488136670 reachable=1048576 distance_sum=88454460920" ;;
      4368004200) echo "h1=4769500031515995996 h2=11687219410050794787 reachable=1048576 distance_sum=118191673143" ;;
    esac
  else
    awk -v s="$2" '$1 == s { printf "h1=%s h2=%s reachable=%s distance_sum=%s", $3, $4, $5, $6; exit }' "$DATA/$1.reference.txt"
  fi
}
verify() {  # label graph rc outfile sources...
  local label=$1 graph=$2 rc=$3 out=$4; shift 4
  local s got w
  for s in "$@"; do
    w=$(want "$graph" "$s")
    got=$(grep -m1 "^BENCH source=$s " "$out" | grep -oE '(h1|h2|reachable|distance_sum)=[0-9]+' | paste -sd' ')
    if [ "$rc" -eq 0 ] && [ -n "$w" ] && [ "$got" = "$w" ]; then
      echo "PASS $label $graph $s $(grep -m1 "^BENCH source=$s " "$out" | grep -oE 'solve_seconds=[0-9.]+')"
    else
      echo "FAIL $label $graph $s rc=$rc"; echo "  want: $w"; echo "  got:  $got"
      tail -15 "$out" | sed 's/^/  | /'; fail=1
    fi
  done
}
launch() {  # outfile nodes ranks_per_node cpus_per_rank minutes command...
  local out=$1 nodes=$2 rpn=$3 cpus=$4 minutes=$5; shift 5
  srun -N "$nodes" -n $((nodes * rpn)) --ntasks-per-node "$rpn" -c "$cpus" --cpu-bind=cores \
    --unbuffered --kill-on-bad-exit=1 --time="$minutes" "$@" > "$out" 2>&1
}
gemini() {  # label graph input vertices nodes rpn cpus binary sources...
  local label=$1 graph=$2 input=$3 n=$4 nodes=$5 rpn=$6 cpus=$7 bin=$8; shift 8
  local out=$LOG/$label-$graph.out
  ( export OMP_NUM_THREADS=$cpus; launch "$out" "$nodes" "$rpn" "$cpus" 20 "$BIN/$bin" "$input" "$n" "$@" )
  local rc=$?
  grep -m1 '^GEMINI load' "$out" | sed "s/^/  $label: /"
  verify "$label" "$graph" $rc "$out" "$@"
}
havoqgt() {  # label graph wsg nodes sources...
  local label=$1 graph=$2 wsg=$3 nodes=$4; shift 4
  local store=$SHM/$graph out=$LOG/$label-$graph.out
  launch "$LOG/$label-$graph.ingest" "$nodes" 56 1 20 "$BIN/havoqgt_ingest" -O -o "$store" "$wsg"
  local rc=$?
  grep -E '^HAVOQGT_INGEST|Number of Delegates' "$LOG/$label-$graph.ingest" | sed "s/^/  $label: /"
  srun -N "$nodes" --ntasks-per-node=1 --overlap du -sh "$SHM" 2>/dev/null | sed "s/^/  $label store: /"
  if [ $rc -ne 0 ]; then
    echo "FAIL $label $graph ingest rc=$rc"; tail -15 "$LOG/$label-$graph.ingest" | sed 's/^/  | /'; fail=1
  else
    local args=() s
    for s in "$@"; do args+=(-s "$s"); done
    launch "$out" "$nodes" 56 1 20 "$BIN/havoqgt_sssp" -i "$store" "${args[@]}"
    rc=$?
    verify "$label" "$graph" $rc "$out" "$@"
  fi
  srun -N "$nodes" --ntasks-per-node=1 --overlap rm -rf "$SHM"
}

for graph in ${GRAPHS//,/ }; do
  n=$(grep -oE 'vertices=[0-9]+' "$DATA/$graph.meta" | cut -d= -f2)
  sources=($(awk '$2 == "tune" { print $1; exit }' "$DATA/$graph.reference.txt")
           $(awk '$2 == "test" { print $1; exit }' "$DATA/$graph.reference.txt"))
  input=$TMP/$graph.gemini32
  [ -s "$input" ] && [ -s "$input.info" ] || OMP_NUM_THREADS=56 "$BIN/wsg_to_gemini" --ids 32 "$DATA/$graph.wsg" "$input"
  echo "--- $graph n=$n sources ${sources[*]}"
  gemini gemini-1Nx1x56 "$graph" "$input" "$n" 1 1 56 gemini_sssp "${sources[@]}"
  gemini gemini-1Nx4x14 "$graph" "$input" "$n" 1 4 14 gemini_sssp "${sources[@]}"
  if [ "$NN" -ge 2 ]; then
    gemini gemini-2Nx1x56 "$graph" "$input" "$n" 2 1 56 gemini_sssp "${sources[@]}"
    gemini gemini-2Nx8x7 "$graph" "$input" "$n" 2 8 7 gemini_sssp "${sources[@]}"
  fi
  havoqgt havoqgt-1Nx56 "$graph" "$DATA/$graph.wsg" 1 "${sources[@]}"
  [ "$NN" -ge 2 ] && havoqgt havoqgt-2Nx56 "$graph" "$DATA/$graph.wsg" 2 "${sources[@]}"
done

if [ "$STRIDED" = 1 ] && [ "$NN" -ge 2 ]; then
  wsg=$CAMPAIGN/test64/mesh20-s4200.wsg
  input=$TMP/mesh20-s4200.gemini64
  echo "--- strided (64-bit ids) $wsg"
  if [ ! -s "$wsg" ]; then
    echo "FAIL strided: $wsg missing (make it with gluon64_test.sbatch / stride_graph.py)"; fail=1
  else
    [ -s "$input" ] && [ -s "$input.info" ] || OMP_NUM_THREADS=56 "$BIN/wsg_to_gemini" --ids 64 "$wsg" "$input"
    n=$(grep -oE '^vertices=[0-9]+' "$input.info" | cut -d= -f2)
    gemini gemini64-2Nx1x56 strided "$input" "$n" 2 1 56 gemini_sssp64 3097941000 4368004200
    havoqgt havoqgt-2Nx56 strided "$wsg" 2 3097941000 4368004200
  fi
fi
[ $fail = 0 ] && echo "SMOKE GEMINI HAVOQGT PASSED" || echo "SMOKE GEMINI HAVOQGT FAILED"
