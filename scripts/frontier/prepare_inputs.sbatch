#!/usr/bin/env bash
# Frontier: every SSSP input the IPDPS27/SC27 campaigns use, in one campaign
# directory. Same preparation as benchmarks/prepare.sbatch,
# scripts/anvil/prepare_large.sbatch and scripts/delta/onenode_{prepare,paper_inputs}.sbatch:
# canonical undirected, minimum-parallel-edge .wsg; independent GAPBS-reader
# Dijkstra references (seed 20260913; ten sources for the step-7.5 graphs,
# six -- two tuning, four held-out -- for the paper graphs); Morton -z
# relabelings of the meshes and roads; Galois .gr copies for the Gluon arms.
#
# The step-7.5 graphs have SHA-256 and reference rows recorded in
# design/step75-data/graphs.json (and .gr hashes for four of them). Each one
# regenerated here is checked against that record, which is what shows the
# Frontier toolchain reproduces the Delta/Anvil inputs bit for bit. The large
# graphs' hashes stayed on Delta scratch, so they are recorded fresh in
# graphs/sha256.txt.
#
# Raw downloads go in CAMPAIGN/raw first (login node; see
# scripts/frontier/README.md). Binaries come from scripts/frontier/build_baselines.sh,
# copied to CAMPAIGN/bin.
#
#   sbatch scripts/frontier/prepare_inputs.sbatch CAMPAIGN [names] [parallel]
#
# names defaults to every graph; a rerun resumes. Frontier nodes have 512 GB;
# rmat27 peaks near 86 GB and rmat26 near 50 GB, so run the RMAT graphs with
# modest parallelism.
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-inputs
set -uo pipefail
ulimit -c 0
CAMPAIGN=${1:?campaign directory}
ALL=mesh20,rmat20,uniform20,mesh22,rmat22,rmat20-s2,uniform20-s2,road-ny,youtube,mesh24,mesh26,rmat24,rmat25,rmat26,rmat27,uniform25,road-usa,road-usa-w4,orkut
NAMES=${2:-$ALL}
PARALLEL=${3:-4}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$CAMPAIGN")/venv/bin/python}
BIN=$CAMPAIGN/bin
DATA=$CAMPAIGN/graphs
RAW=$CAMPAIGN/raw
mkdir -p "$DATA"
export OMP_NUM_THREADS=${OMP_NUM_THREADS:-16} PYTHONNOUSERSITE=1 OPENBLAS_NUM_THREADS=1
export APP PY BIN DATA RAW

generate() {  # name mode vertices seed
  "$BIN/prepare_graph" gen "$2" "$3" "$4" "$DATA/$1.wsg.tmp" > "$DATA/$1.meta.tmp"
}
dimacs() {    # name file vertices [awk weight filter]
  gzip -dc "$RAW/$2" | { if [ -n "${4:-}" ]; then awk "$4"; else cat; fi; } |
    "$BIN/prepare_graph" dimacs "$3" 0 1 "$DATA/$1.wsg.tmp" > "$DATA/$1.meta.tmp"
}
meshz() {     # name vertices seed: Morton-ordered mesh written directly (prepare_graph meshz)
  "$BIN/prepare_graph" meshz 2 "$2" "$3" "$DATA/$1.wsg.tmp" > "$DATA/$1.meta.tmp"
}
snap() {      # name file vertices
  gzip -dc "$RAW/$2" | "$BIN/prepare_graph" snap "$3" 0 1 "$DATA/$1.wsg.tmp" > "$DATA/$1.meta.tmp"
}
prepare_one() {
  local name=$1 sources=6 range=
  local base=$DATA/$name
  case $name in
    mesh20|rmat20|uniform20|mesh22|rmat22|rmat20-s2|uniform20-s2|road-ny|youtube) sources=10 ;;
  esac
  case $name in road-usa|road-eu|road-na) range=any-range ;; esac  # distances exceed binary32; ACIC/GAPBS/Gluon only
  if [ ! -s "$base.wsg" ] || [ ! -s "$base.meta" ]; then
    case $name in
      uniform20)    generate "$name" 1 1048576 1 ;;
      rmat20)       generate "$name" 3 1048576 1 ;;
      mesh20)       generate "$name" 2 1048576 1 ;;
      rmat22)       generate "$name" 3 4194304 1 ;;
      mesh22)       generate "$name" 2 4194304 1 ;;
      rmat20-s2)    generate "$name" 3 1048576 2 ;;
      uniform20-s2) generate "$name" 1 1048576 2 ;;
      mesh24)       generate "$name" 2 16777216 1 ;;
      mesh26)       generate "$name" 2 67108864 1 ;;
      # Larger meshes are generated in Morton order directly: byte-identical
      # to gen + reorder_graph.py at 2^24 and 2^26, which do not fit past 2^28.
      mesh28-z)     meshz "$name" 268435456 1 ;;
      mesh30-z)     meshz "$name" 1073741824 1 ;;
      rmat24)       generate "$name" 3 16777216 1 ;;
      rmat25)       generate "$name" 3 33554432 1 ;;
      rmat26)       generate "$name" 3 67108864 1 ;;
      rmat27)       generate "$name" 3 134217728 1 ;;
      uniform25)    generate "$name" 1 33554432 1 ;;
      road-ny)      dimacs "$name" USA-road-d.NY.gr.gz 264346 ;;
      road-usa)     dimacs "$name" USA-road-d.USA.gr.gz 23947347 ;;
      # road-usa's topology with each native weight divided by four, rounded
      # up, so every distance stays below 2^24 and RIKEN's binary32 path is exact.
      road-usa-w4)  dimacs "$name" USA-road-d.USA.gr.gz 23947347 '$1 == "a" { $4 = int(($4 + 3) / 4) } { print }' ;;
      # OSM car graphs, largest connected component (prepare_osm.sbatch).
      road-eu)      dimacs "$name" OSM-road-d.EU.gr.gz "$(cat "$RAW/OSM-road-d.EU.vertices")" ;;
      road-na)      dimacs "$name" OSM-road-d.NA.gr.gz "$(cat "$RAW/OSM-road-d.NA.vertices")" ;;
      youtube)      snap "$name" com-youtube.ungraph.txt.gz 1134890 ;;
      orkut)        snap "$name" com-orkut.ungraph.txt.gz 3072441 ;;
      *) echo "UNKNOWN $name"; return 1 ;;
    esac || { echo "PREPARE FAILED $name"; return 1; }
    mv "$base.wsg.tmp" "$base.wsg" && mv "$base.meta.tmp" "$base.meta"
  fi
  if [ ! -s "$base.reference.txt" ]; then
    "$BIN/reference_gap" "$base.wsg" "$sources" $range > "$base.reference.tmp" 2> "$base.reference.log" &&
      mv "$base.reference.tmp" "$base.reference.txt" || { echo "REFERENCE FAILED $name"; return 1; }
  fi
  # Morton relabelings (E2, change 1): meshes by grid position, roads by
  # DIMACS coordinates. Same physical sources; the script checks that the
  # distance sums and maxima equal the native graph's.
  local z=
  case $name in
    mesh24|mesh26) z=(mesh "$((1 << (${name#mesh} / 2)))") ;;
    road-usa|road-usa-w4) z=(coords "$RAW/USA-road-d.USA.co.gz") ;;
    road-eu) z=(coords "$RAW/OSM-road-d.EU.co.gz") ;;
    road-na) z=(coords "$RAW/OSM-road-d.NA.co.gz") ;;
  esac
  if [ -n "$z" ] && [ ! -s "$base-z.reference.txt" ]; then
    "$PY" "$APP/benchmarks/reorder_graph.py" "$CAMPAIGN" "$name" "$name-z" "${z[@]}" \
      > "$base-z.reorder.log" 2>&1 || { echo "REORDER FAILED $name"; return 1; }
  fi
  echo "PREPARED $name $(date -Is)"
}
galois_one() {
  [ -e "$DATA/$1.gr.done" ] && return
  "$PY" "$APP/benchmarks/to_galois.py" "$DATA/$1.wsg" && touch "$DATA/$1.gr.done" ||
    { echo "CONVERSION FAILED $1"; return 1; }
}
export -f generate dimacs snap meshz prepare_one galois_one
export CAMPAIGN

printf '%s\n' ${NAMES//,/ } | xargs -P "$PARALLEL" -I{} bash -c 'prepare_one {}'

# Galois copies of every prepared graph and Morton relabeling (Gluon reads .gr).
for n in ${NAMES//,/ }; do
  for g in "$n" "$n-z"; do [ -s "$DATA/$g.wsg" ] && echo "$g"; done
done | xargs -P "$PARALLEL" -I{} bash -c 'galois_one {}'

( cd "$DATA" && sha256sum *.wsg *.gr 2>/dev/null ) > "$DATA/sha256.txt"

# Check the step-7.5 graphs against the Delta/Anvil record.
"$PY" - "$APP/design/step75-data/graphs.json" "$DATA" <<'PY'
import hashlib, json, sys
from pathlib import Path
record, data = json.load(open(sys.argv[1])), Path(sys.argv[2])
def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 24), b''): h.update(chunk)
    return h.hexdigest()
def rows(text):  # drop the timing line; keep the digest table
    return [l for l in text.splitlines() if l and not l.startswith('Read Time')]
bad = 0
for name, want in record.items():
    wsg = data / f'{name}.wsg'
    if not wsg.exists(): continue
    checks = {'wsg': sha(wsg) == want['sha256'],
              'meta': (data / f'{name}.meta').read_text() == want['metadata'],
              'reference': rows((data / f'{name}.reference.txt').read_text()) == rows(want['reference'])}
    if 'gluon_sha256' in want and (data / f'{name}.gr').exists():
        checks['gr'] = sha(data / f'{name}.gr') == want['gluon_sha256']
    ok = all(checks.values()); bad += not ok
    print(('MATCH ' if ok else 'MISMATCH ') + name, ' '.join(f'{k}={"ok" if v else "DIFF"}' for k, v in checks.items()))
print('RECORD CHECK', 'PASSED' if not bad else f'FAILED ({bad})')
PY
for n in ${NAMES//,/ }; do printf '%-14s %s' "$n" "$(cat "$DATA/$n.meta" 2>/dev/null)"; done
printf 'INPUTS COMPLETE\n'
