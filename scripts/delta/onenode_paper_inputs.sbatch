#!/usr/bin/env bash
# Prepare the remaining paper inputs on work storage, one graph at a time.
# Download SNAP com-orkut.ungraph.txt.gz into ROOT/raw before submission.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=190G
#SBATCH --time=04:00:00
#SBATCH --job-name=acic-paper-inputs
set -euo pipefail
ulimit -c 0
ROOT=${1:?work campaign directory}
GRAPHS=${2:-road-usa-w4,orkut,rmat25,uniform25,rmat26,rmat27}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export PYTHONNOUSERSITE=1 OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=16
mkdir -p "$ROOT/graphs"
IFS=, read -r -a graphs <<< "$GRAPHS"
for graph in "${graphs[@]}"; do
  base=$ROOT/graphs/$graph
  if [ ! -s "$base.wsg" ] || [ ! -s "$base.meta" ]; then
    case "$graph" in
      rmat25|rmat26|rmat27|uniform25)
        if [[ $graph == rmat* ]]; then mode=3; scale=${graph#rmat};
        else mode=1; scale=${graph#uniform}; fi
        "$ROOT/bin/prepare_graph" gen "$mode" "$((1 << scale))" 1 "$base.wsg.tmp" > "$base.meta.tmp" ;;
      road-usa-w4)
        gzip -dc "$ROOT/raw/USA-road-d.USA.gr.gz" |
          awk '$1 == "a" { $4 = int(($4 + 3) / 4) } { print }' |
          "$ROOT/bin/prepare_graph" dimacs 23947347 0 1 "$base.wsg.tmp" > "$base.meta.tmp" ;;
      orkut)
        gzip -dc "$ROOT/raw/com-orkut.ungraph.txt.gz" |
          "$ROOT/bin/prepare_graph" snap 3072441 0 1 "$base.wsg.tmp" > "$base.meta.tmp" ;;
      *) echo "Unknown paper graph: $graph" >&2; exit 2 ;;
    esac
    mv "$base.wsg.tmp" "$base.wsg"
    mv "$base.meta.tmp" "$base.meta"
  fi
  if [ ! -s "$base.reference.txt" ]; then
    "$ROOT/bin/reference_gap" "$base.wsg" 6 > "$base.reference.tmp"
    mv "$base.reference.tmp" "$base.reference.txt"
  fi
  if [ "$graph" = road-usa-w4 ] && [ ! -s "$base-z.reference.txt" ]; then
    python3 "$APP/benchmarks/reorder_graph.py" "$ROOT" "$graph" "$graph-z" \
      coords "$ROOT/raw/USA-road-d.USA.co.gz"
  fi
  sha256sum "$base.wsg"
  echo "PREPARED $graph"
done
