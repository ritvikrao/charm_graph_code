#!/usr/bin/env bash
# IPDPS sprint: independent validation of the paper graphs' reference digests
# (benchmarks/validate_independent.py) -- raw downloads or a numpy
# re-implementation of the generators, scipy Dijkstra, no campaign C++.
#
#   sbatch scripts/anvil/validate_independent.sbatch /anvil/scratch/$USER/acic/campaign
#
# GRAPHS overrides the list; each graph runs on its test sources.
#SBATCH --account=asc050025
#SBATCH --partition=shared
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=48
#SBATCH --mem=90G
#SBATCH --time=02:30:00
#SBATCH --job-name=validate-indep
set -uo pipefail
ROOT=${1:?campaign root}
DEPS=${ACIC_BENCH_DEPS:-$(dirname "$ROOT")/deps}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
module load python/3.9.5 >/dev/null 2>&1
GRAPHS=${GRAPHS:-"road-usa orkut mesh24 mesh26 uniform25 rmat25"}
OUT=$ROOT/validation
mkdir -p "$OUT"
rc=0
for g in $GRAPHS; do
  case $g in
    road-usa) spec="dimacs $DEPS/USA-road-d.USA.gr.gz" ;;
    road-usa-w4) spec="dimacs $DEPS/USA-road-d.USA.gr.gz 4" ;;
    road-ny) spec="dimacs $DEPS/USA-road-d.NY.gr.gz" ;;
    orkut) spec="snap $DEPS/com-orkut.ungraph.txt.gz 1" ;;
    mesh24) spec="mesh 16777216 1" ;;
    mesh26) spec="mesh 67108864 1 --no-raw-stats" ;;
    uniform25) spec="uniform 33554432 1 --no-raw-stats" ;;
    rmat25) spec="rmat 33554432 1 --no-raw-stats" ;;
    *) echo "no recipe for $g"; rc=1; continue ;;
  esac
  /usr/bin/time -v python3 "$APP/benchmarks/validate_independent.py" "$g" $spec \
    --campaign "$ROOT" --sources test > "$OUT/$g-$SLURM_JOB_ID.txt" 2>&1 || rc=1
  grep -E 'RAW|CANON|SOURCE|RESULT|Maximum resident|Elapsed' "$OUT/$g-$SLURM_JOB_ID.txt"
done
exit $rc
