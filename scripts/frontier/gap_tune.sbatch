#!/usr/bin/env bash
# Frontier GAPBS reference (J2): joint thread/delta selection on training
# sources only, then the frozen winner on four held-out sources.
# Delta's frozen settings (mesh 128 threads, road 64) exceed Frontier's 56
# usable cores, so the reference must be re-selected here before any C6 claim.
#
# Thread candidates are bounded to keep the job inside Frontier's two-hour
# limit for small node counts: every launch re-reads the graph (mesh26-z is
# 2.7 GB), which dominates the solve.
#
# ENGINE=wasp tunes the Wasp SC25 artifact (bin/wasp_sssp) the same way.
# SELECTION_JOB=<job> skips the search and reruns that job's frozen selection
# on the held-out sources, as a second allocation of the reference.
#
#   sbatch scripts/frontier/gap_tune.sbatch CAMPAIGN [graphs] [threads]
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-gap-tune
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
GRAPHS=${2:-mesh26-z,road-usa-z}
THREADS=${3:-14,28,42,56}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 libfabric/2.3.1 >/dev/null 2>&1
export OMP_PLACES=cores OMP_PROC_BIND=close NO_AFFINITY=1
"$PY" "$APP/benchmarks/onenode_gap_tune.py" "$ROOT" --graphs "$GRAPHS" \
  --threads "$THREADS" --sources 4 --reps 3 --timeout 300 --launch-timeout 180 --engine "${ENGINE:-gap}" \
  ${SELECTION_JOB:+--selection-job "$SELECTION_JOB"}
echo "GAP TUNE COMPLETE $SLURM_JOB_ID"
