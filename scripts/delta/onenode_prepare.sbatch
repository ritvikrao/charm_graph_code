#!/usr/bin/env bash
# Required raw DIMACS files live in CAMPAIGN/raw (download on the login host).
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=200G
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-gap-inputs
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign directory}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
GRAPHS=${2:-mesh24,mesh26,road-usa}
export PYTHONNOUSERSITE=1 OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1
mkdir -p "$ROOT/graphs"
IFS=, read -r -a graphs <<< "$GRAPHS"
for graph in "${graphs[@]}"; do
  if [ ! -s "$ROOT/graphs/$graph.wsg" ]; then
    case "$graph" in
      mesh24|mesh26)
        scale=${graph#mesh}
        "$ROOT/bin/prepare_graph" gen 2 "$((1 << scale))" 1 "$ROOT/graphs/$graph.wsg" > "$ROOT/graphs/$graph.meta" ;;
      road-usa)
        gzip -dc "$ROOT/raw/USA-road-d.USA.gr.gz" |
          "$ROOT/bin/prepare_graph" dimacs 23947347 0 1 "$ROOT/graphs/$graph.wsg" > "$ROOT/graphs/$graph.meta" ;;
      *) echo "Unknown graph: $graph" >&2; exit 2 ;;
    esac
  fi
  if [ ! -s "$ROOT/graphs/$graph.reference.txt" ]; then
    "$ROOT/bin/reference_gap" "$ROOT/graphs/$graph.wsg" 6 any-range > "$ROOT/graphs/$graph.reference.tmp"
    mv "$ROOT/graphs/$graph.reference.tmp" "$ROOT/graphs/$graph.reference.txt"
  fi
  if [ ! -s "$ROOT/graphs/$graph-z.reference.txt" ]; then
    if [[ $graph == mesh* ]]; then
      scale=${graph#mesh}; args=(mesh "$((1 << (scale / 2)))")
    else
      args=(coords "$ROOT/raw/USA-road-d.USA.co.gz")
    fi
    python3 "$APP/benchmarks/reorder_graph.py" "$ROOT" "$graph" "$graph-z" "${args[@]}"
  fi
  sha256sum "$ROOT/graphs/$graph.wsg" "$ROOT/graphs/$graph-z.wsg"
done
