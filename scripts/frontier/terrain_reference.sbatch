#!/usr/bin/env bash
# Reference rows for a graph no single node can hold (terrain-ae-z): ACIC
# (acic_w64m) solves every source in NAME.sources with --certify, and a row is
# written only for a certified solve. The certificate is a proof, checked
# edge by edge across the distributed graph, that the distances are exact:
# d(source) = 0, every weight positive, no edge u->v with d(u) + w < d(v), and
# every reached vertex other than the source has an edge with d(u) + w = d(v).
# Rows carry the digest, reachable count, distance sum, max distance and
# reachable arcs, as reference_gap writes them; the first two sources are the
# tuning sources.
#
# Every later solve (ACIC at other node counts, Gluon) is checked against these
# rows, so a second node count here is not needed; RERUN=1 still compares a new
# run with an existing file instead of writing one.
#
#   sbatch -N 16 scripts/frontier/terrain_reference.sbatch CAMPAIGN [NAME]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=16
#SBATCH --time=01:30:00
#SBATCH --job-name=acic-terrain-ref
set -uo pipefail
ulimit -c 0
ROOT=${1:?campaign}
NAME=${2:-terrain-ae-z}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
N=$SLURM_NNODES
RPN=8
G=$ROOT/graphs/$NAME
SOURCES=$(tr ' ' ',' < "$G.sources")
FIRST=${SOURCES%%,*}
OUT=$ROOT/logs/terrain-reference-$NAME-${N}n-$SLURM_JOB_ID.out
echo "job=$SLURM_JOB_ID hosts=$SLURM_JOB_NODELIST sources=$SOURCES"
sha256sum "$ROOT/bin/acic_w64m"
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
export PMI_MAX_KVS_ENTRIES=$(( 2 * N * RPN * N * RPN > 100000 ? 2 * N * RPN * N * RPN : 100000 ))
export FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
/usr/bin/time -v srun -N "$N" -n $((N * RPN)) --ntasks-per-node $RPN -c 7 --cpu-bind=none --unbuffered \
  --kill-on-bad-exit=1 bash "$APP/benchmarks/launch_acic.sh" $RPN 56 "$ROOT/bin/acic_w64m" 0 "$G.wsg" 1 "$FIRST" 4 0.999 0.005 \
  --result-digest --timeout 3000 --process-share auto --reader-tile auto --slack-control off \
  --process-queue nearest --process-queue-batch 8 +old-scheduler --heap-slice 8 \
  --sources "$SOURCES" --certify > "$OUT" 2>&1
echo "acic exit $?"
grep -E '^(SOURCE_RUN|Compute time|Setup time|Read time|VERIFY parallel|CERTIFY)|Maximum resident|Elapsed' "$OUT" | cut -c1-220
"$PY" - "$OUT" "$G" "$SOURCES" "${RERUN:-}" <<'E'
import re, sys
from pathlib import Path
out, g, sources, rerun = Path(sys.argv[1]).read_text(), sys.argv[2], sys.argv[3].split(','), sys.argv[4]
digests = [dict((k, int(v)) for k, v in re.findall(r'(h1|h2|reachable|distance_sum)=(\d+)', l))
           for l in re.findall(r'^VERIFY parallel digest (.*)$', out, re.M)]
certs = re.findall(r'^CERTIFY edges=(\d+) violations=(\d+) untight=(\d+) bad_source=(\d+) '
                   r'nonpositive_weights=(\d+) max_distance=(\d+)', out, re.M)
verdicts = re.findall(r'^CERTIFY (PASS|FAIL)$', out, re.M)
if not (len(digests) == len(certs) == len(verdicts) == len(sources)) or set(verdicts) != {'PASS'}:
    sys.exit(f'REFERENCE FAILED: {len(digests)} digests, {len(certs)} certificates, verdicts {verdicts}')
rows = ['source\trole\th1\th2\treachable\tdistance_sum\tmax_distance\treachable_arcs']
for i, (s, d, c) in enumerate(zip(sources, digests, certs)):
    rows.append('\t'.join(map(str, [s, 'tune' if i < 2 else 'test', d['h1'], d['h2'], d['reachable'],
                                    d['distance_sum'], c[5], c[0]])))
text = '\n'.join(rows) + '\n'
ref = Path(g + '.reference.txt')
if rerun:
    old = ref.read_text()
    same = old[old.index('source\t'):] == text
    print('REFERENCE RERUN', 'MATCHES' if same else 'DIFFERS')
    sys.exit(0 if same else 1)
if ref.exists():
    sys.exit(f'{ref} exists; set RERUN=1 to compare')
ref.write_text(f'# certified by ACIC --certify ({Path(sys.argv[1]).name})\n' + text)
print(text, end='')
print('REFERENCE WRITTEN', ref)
E
