#!/usr/bin/env bash
# 64-bit vertex ids: correctness of the WIRE=compact64 build (acic_w64) and
# the --certify proof, against the compact build (acic_w32) of the same
# source. Two nodes, so updates cross the network.
#
#  1. Campaign inputs, two held-out sources each: both binaries return the
#     reference digest, and --certify passes on every solve.
#  2. The same generated graphs written with 32-bit and 64-bit ids
#     (graph_convert gen ... --wide): identical digests, serial --verify passes
#     on the 32-bit file, --certify on both.
#  3. The certificate can fail: ACIC_CERTIFY_PERTURB must give CERTIFY FAIL.
#
#   sbatch -N 2 scripts/frontier/wire64_test.sbatch CAMPAIGN
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=2
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-w64test
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
N=$SLURM_NNODES
RPN=4
WORKERS=56
FLAGS="--process-share auto --reader-tile auto --slack-control off --process-queue nearest --process-queue-batch 8 +old-scheduler --heap-slice 8"
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
OUT=$ROOT/logs/wire64-test-$SLURM_JOB_ID
mkdir -p "$OUT"
echo "job=$SLURM_JOB_ID hosts=$SLURM_JOB_NODELIST"
sha256sum "$ROOT/bin/acic_w32" "$ROOT/bin/acic_w64"
EXTRA=()
FAIL=0
run() {  # run LABEL BINARY FILE SOURCES [flags...] -> OUT/LABEL.out
  local label=$1 bin=$2 file=$3 sources=$4; shift 4
  srun -N "$N" -n $((N * RPN)) --ntasks-per-node "$RPN" -c $((WORKERS / RPN)) --cpu-bind=none \
    --unbuffered --kill-on-bad-exit=1 --time=00:05:00 \
    bash "$APP/benchmarks/launch_acic.sh" "$RPN" "$WORKERS" "$ROOT/bin/$bin" 0 "$file" 1 "${sources%%,*}" 4 0.999 0.005 \
    --result-digest --timeout 120 $FLAGS --sources "$sources" "$@" > "$OUT/$label.out" 2>&1
}
digests() { grep -o 'VERIFY parallel digest.*' "$1"; }
check() {  # check LABEL CONDITION-DESCRIPTION STATUS
  if [ "$3" = 0 ]; then echo "PASS $1: $2"; else echo "FAIL $1: $2"; FAIL=1; fi
}

# 1. Campaign inputs against their references.
for graph in road-ny youtube mesh22 rmat22; do
  ref=$ROOT/graphs/$graph.reference.txt
  sources=$(awk -F'\t' '$2 == "test" {print $1}' "$ref" | head -2 | paste -sd,)
  for bin in acic_w32 acic_w64; do
    label=$graph-$bin
    run "$label" "$bin" "$ROOT/graphs/$graph.wsg" "$sources" --certify; status=$?
    python3 - "$OUT/$label.out" "$ref" "$sources" <<'EOF'
import re, sys
text, ref, sources = open(sys.argv[1]).read(), sys.argv[2], sys.argv[3].split(',')
refs = {r.split()[0]: r.split()[2:6] for r in open(ref) if r[:1].isdigit()}
got = re.findall(r'VERIFY parallel digest h1=(\d+) h2=(\d+) reachable=(\d+) distance_sum=(\d+)', text)
passes = len(re.findall(r'^CERTIFY PASS', text, re.M))
ok = [list(g) for g in got] == [refs[s] for s in sources] and passes == len(sources)
print(f'  digests match reference: {[list(g) for g in got] == [refs[s] for s in sources]}, certify passes: {passes}/{len(sources)}')
print('  ' + '\n  '.join(re.findall(r'^CERTIFY edges.*$', text, re.M)))
sys.exit(0 if ok else 1)
EOF
    check "$label" "exit $status, reference digests and certificate" $(( status || $? ))
  done
done

# 2. 32-bit and 64-bit id files of the same generated graphs.
T=$ROOT/graphs/test64
for g in mesh20g rmat20g; do
  src=5,700020  # both have out-degree >= the mean (graph_convert source; offsets)
  run "$g-narrow-verify" acic_w32 "$T/$g.wsg" "$src" --verify --certify; s1=$?
  run "$g-wide-w64" acic_w64 "$T/$g-wide.wsg" "$src" --certify; s2=$?
  run "$g-wide-w32" acic_w32 "$T/$g-wide.wsg" "$src" --certify; s3=$?
  grep -h '^VERIFY PASS\|^VERIFY FAIL' "$OUT/$g-narrow-verify.out" | sort | uniq -c
  same=1
  [ "$(digests "$OUT/$g-narrow-verify.out")" = "$(digests "$OUT/$g-wide-w64.out")" ] &&
    [ "$(digests "$OUT/$g-narrow-verify.out")" = "$(digests "$OUT/$g-wide-w32.out")" ] &&
    [ "$(grep -c '^VERIFY PASS' "$OUT/$g-narrow-verify.out")" = 2 ] &&
    [ "$(cat "$OUT"/$g-*.out | grep -c '^CERTIFY PASS')" = 6 ] && same=0
  check "$g" "exits $s1/$s2/$s3; serial verify, identical digests across id widths and binaries, 6 certificates" $(( s1 || s2 || s3 || same ))
done

# 3. The certificate must catch a wrong distance.
ACIC_CERTIFY_PERTURB=123457 run perturb acic_w64 "$ROOT/graphs/mesh22.wsg" \
  "$(awk -F'\t' '$2 == "test" {print $1; exit}' "$ROOT/graphs/mesh22.reference.txt")" --certify; status=$?
grep -h '^CERTIFY' "$OUT/perturb.out"
[ "$status" != 0 ] && grep -q '^CERTIFY FAIL' "$OUT/perturb.out"; check perturb "a perturbed distance fails the certificate (exit $status)" $?

echo "WIRE64 TEST $([ $FAIL = 0 ] && echo PASSED || echo FAILED) $SLURM_JOB_ID"
