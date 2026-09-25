#!/usr/bin/env bash
# Correctness of the 64-bit Gluon build (gluon_sssp64 = gluon.patch +
# gluon64.patch) and of ACIC reading a wide .wsg, before the terrain graph.
# Four nodes (the strided graph has 4.4e9 ids, mostly isolated), 8 ranks each.
#
#  A. No regression below 2^32: stock gluon_sssp and gluon_sssp64 on the
#     version-1 .gr of mesh24-z and road-usa-z, and gluon_sssp64 on their
#     version-2 copies (to_galois.py --v2): every digest equals the reference.
#     gluon_sssp64 with cvc on mesh24-z too (cvc stays usable below 2^32).
#  B. Past 2^32: mesh20 with ids multiplied by 4200 (stride_graph.py; 4.4e9
#     ids, isolated between), sources 737605 and 1040001 (x 4200; the second
#     lands above 2^32). reachable, distance_sum and max distance must equal
#     scipy's Dijkstra on mesh20 itself; ACIC (acic_w64m --certify) must pass
#     its certificate; gluon_sssp64 (oec, version-2 .gr) must match ACIC's
#     id-keyed h1/h2 as well.
#  C. gluon_sssp64 must refuse cvc on the strided graph.
#
#   sbatch -N 4 scripts/frontier/gluon64_test.sbatch CAMPAIGN
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=4
#SBATCH --time=00:45:00
#SBATCH --job-name=acic-gluon64
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
G=$ROOT/graphs
T=$ROOT/test64
S=4200
LO=737605
HI=1040001
mkdir -p "$T"
echo "job=$SLURM_JOB_ID hosts=$SLURM_JOB_NODELIST"
sha256sum "$ROOT/bin/gluon_sssp" "$ROOT/bin/gluon_sssp64" "$ROOT/bin/acic_w64m"

# --- inputs
[ -s "$T/mesh20-s$S.wsg" ] || "$PY" "$APP/benchmarks/stride_graph.py" "$G/mesh20.wsg" $S "$T/mesh20-s$S.wsg"
[ -s "$T/mesh20-s$S.gr" ] || "$PY" "$APP/benchmarks/to_galois.py" "$T/mesh20-s$S.wsg"
for g in mesh24-z road-usa-z; do
  [ -s "$T/$g.v2.gr" ] || { ln -sf "$G/$g.wsg" "$T/$g.wsg"; "$PY" "$APP/benchmarks/to_galois.py" --v2 "$T/$g.wsg"; }
done
"$PY" - "$G/mesh20.wsg" $LO $HI > "$T/expected-$SLURM_JOB_ID.txt" <<'E'
import sys, struct, numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import dijkstra
path = sys.argv[1]
with open(path, 'rb') as f:
    d, m, n = struct.unpack('=Bqq', f.read(17))
off = np.fromfile(path, '<i8', n+1, offset=17)
e = np.fromfile(path, np.dtype([('v', '<i4'), ('w', '<i4')]), m, offset=17+(n+1)*8)
A = csr_matrix((e['w'].astype(np.float64), e['v'], off), shape=(n, n))
for s in map(int, sys.argv[2:]):
    dist = dijkstra(A, indices=s)
    ok = np.isfinite(dist)
    print(s, int(ok.sum()), int(dist[ok].sum()), int(dist[ok].max()))
E
cat "$T/expected-$SLURM_JOB_ID.txt"

gluon() {  # LABEL BINARY GRFILE SOURCE DELTA PARTITION
  ( module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
    module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
    module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
    export MPICH_OFI_NIC_POLICY=ROUND-ROBIN
    unset LCT_PMI_BACKEND ACIC_SRUN_MPI
    srun -N "$SLURM_NNODES" -n $((SLURM_NNODES * 8)) --ntasks-per-node 8 -c 7 --cpu-bind=cores --unbuffered --kill-on-bad-exit=1 --time=00:10:00 \
      "$ROOT/bin/$2" "$3" -symmetricGraph -exec=Async -startNode="$4" -t=6 -runs=1 \
      -maxIterations=2147483647 -delta="$5" -partition="$6" > "$T/$1-$SLURM_JOB_ID.out" 2>&1 )
  echo "$1: exit $? $(grep -m1 '^BENCH' "$T/$1-$SLURM_JOB_ID.out") $(grep -m1 'Max distance' "$T/$1-$SLURM_JOB_ID.out")"
}

# --- A
for spec in mesh24-z:64 road-usa-z:524288; do
  g=${spec%%:*}; delta=${spec#*:}
  src=$(awk -F'\t' '$2 == "test" {print $1; exit}' "$G/$g.reference.txt")
  gluon "A-$g-stock-v1" gluon_sssp "$G/$g.gr" "$src" "$delta" oec
  gluon "A-$g-g64-v1" gluon_sssp64 "$G/$g.gr" "$src" "$delta" oec
  gluon "A-$g-g64-v2" gluon_sssp64 "$T/$g.v2.gr" "$src" "$delta" oec
done
src=$(awk -F'\t' '$2 == "test" {print $1; exit}' "$G/mesh24-z.reference.txt")
gluon "A-mesh24-z-g64-v1-cvc" gluon_sssp64 "$G/mesh24-z.gr" "$src" 64 cvc

# --- B
( module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
  module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
  export PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
  unset LCT_PMI_BACKEND ACIC_SRUN_MPI
  srun -N "$SLURM_NNODES" -n $((SLURM_NNODES * 8)) --ntasks-per-node 8 -c 7 --cpu-bind=none --unbuffered --kill-on-bad-exit=1 --time=00:15:00 \
    bash "$APP/benchmarks/launch_acic.sh" 8 56 "$ROOT/bin/acic_w64m" 0 "$T/mesh20-s$S.wsg" 1 $((LO * S)) 4 0.999 0.005 \
    --result-digest --timeout 600 --process-share auto --reader-tile auto --slack-control off \
    --process-queue nearest --process-queue-batch 8 +old-scheduler --heap-slice 8 \
    --sources $((LO * S)),$((HI * S)) --certify > "$T/B-acic-$SLURM_JOB_ID.out" 2>&1
  echo "B-acic: exit $?"; grep -E '^(VERIFY parallel|CERTIFY)' "$T/B-acic-$SLURM_JOB_ID.out" )
gluon "B-g64-lo" gluon_sssp64 "$T/mesh20-s$S.gr" $((LO * S)) 64 oec
gluon "B-g64-hi" gluon_sssp64 "$T/mesh20-s$S.gr" $((HI * S)) 64 oec

# --- C
gluon "C-g64-cvc" gluon_sssp64 "$T/mesh20-s$S.gr" $((LO * S)) 64 cvc
grep -q 'only the oec partition' "$T/C-g64-cvc-$SLURM_JOB_ID.out" && echo "C: refused as it should" || echo "C: NOT REFUSED"

# --- verdict
"$PY" - "$T" "$SLURM_JOB_ID" "$G" $S <<'E'
import re, sys
from pathlib import Path
T, job, G, S = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3]), int(sys.argv[4])
ok = True
def bench(label):
    m = re.search(r'^BENCH (.*)$', (T/f'{label}-{job}.out').read_text(), re.M)
    return {k: int(v) for k, v in re.findall(r'(h1|h2|reachable|distance_sum)=(\d+)', m[1])} if m else None
for g in ['mesh24-z', 'road-usa-z']:
    rows = (G/f'{g}.reference.txt').read_text().splitlines()
    head = rows.index(next(r for r in rows if r.startswith('source\t')))
    keys = rows[head].split('\t')
    ref = next(dict(zip(keys, r.split('\t'))) for r in rows[head+1:] if r.split('\t')[1] == 'test')
    want = {k: int(ref[k]) for k in ['h1', 'h2', 'reachable', 'distance_sum']}
    for arm in ['stock-v1', 'g64-v1', 'g64-v2'] + (['g64-v1-cvc'] if g == 'mesh24-z' else []):
        got = bench(f'A-{g}-{arm}')
        good = got == want
        ok &= good
        print(f'A {g} {arm}: {"PASS" if good else "FAIL"} {got}')
exp = {int(l.split()[0]) * S: list(map(int, l.split()[1:])) for l in (T/f'expected-{job}.txt').read_text().split('\n') if l.strip()}
acic = (T/f'B-acic-{job}.out').read_text()
digests = [dict((k, int(v)) for k, v in re.findall(r'(h1|h2|reachable|distance_sum)=(\d+)', l))
           for l in re.findall(r'^VERIFY parallel digest (.*)$', acic, re.M)]
certs = re.findall(r'^CERTIFY edges=(\d+) violations=(\d+) untight=(\d+) bad_source=(\d+) nonpositive_weights=(\d+) max_distance=(\d+)', acic, re.M)
passes = len(re.findall(r'^CERTIFY PASS', acic, re.M))
print(f'B acic: {len(digests)} digests, {passes} certificates passed')
ok &= passes == 2 and len(digests) == 2
for (src, (reach, dsum, dmax)), dg, cert, label in zip(exp.items(), digests, certs, ['lo', 'hi']):
    good = dg['reachable'] == reach and dg['distance_sum'] == dsum and int(cert[5]) == dmax
    g64 = bench(f'B-g64-{label}')
    same = g64 == dg
    ok &= good and same
    print(f'B {label} source={src}: acic vs scipy {"PASS" if good else "FAIL"} '
          f'(reachable {dg["reachable"]}/{reach}, sum {dg["distance_sum"]}/{dsum}, max {cert[5]}/{dmax}); '
          f'gluon64 vs acic {"PASS" if same else "FAIL"} {g64}')
refused = 'only the oec partition' in (T/f'C-g64-cvc-{job}.out').read_text()
ok &= refused
print('GLUON64 TEST', 'PASSED' if ok else 'FAILED', job)
E
