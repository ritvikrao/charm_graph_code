#!/usr/bin/env bash
# Two training sources, three timed repeats, fixed queue-policy comparison.
# Submit twice at one node and twice at eight nodes. Total cap: 256 SU.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=00:12:00
#SBATCH --job-name=acic-R1
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
VARIANTS=${2:?variants JSON}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
for graph in mesh26-z road-usa-z; do
  python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$VARIANTS" \
    --sources 2 --reps 3 --source-role tune --batch
  python3 "$APP/benchmarks/work_cost_report.py" "$ROOT/logs/AB-$graph-${SLURM_NNODES}n-$SLURM_JOB_ID"
done
