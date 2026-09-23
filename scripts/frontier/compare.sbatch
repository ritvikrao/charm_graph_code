#!/usr/bin/env bash
# Frontier counterpart of scripts/anvil/compare.sbatch, for run.py's external
# comparison: each baseline tunes its own layout and parameters on the training
# sources, then runs the selection on held-out sources (run.py --mode external).
#
# Frontier differences: Cray PMI (never --mpi=pmi2), 56 usable cores per node
# (RIKEN layouts 2, 4, 8 or 56 ranks per node, from machine.py), and Cray MPICH
# spreads ranks over the NICs only with MPICH_OFI_NIC_POLICY=ROUND-ROBIN when a
# rank spans NUMA domains.
#
# benchmarks/run.py is read live from APP when the job starts. Do not edit it
# while a job that uses it is queued.
#
#   sbatch -N 16 --time=02:00:00 scripts/frontier/compare.sbatch CAMPAIGN \
#       --mode external --external-arms riken --workers 56 --timeout 180 \
#       --sources 4 --reps 3 --graphs mesh26-z,orkut,rmat25,uniform25
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-compare
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
shift
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
PY=${ACIC_PYTHON:-$(dirname "$ROOT")/venv/bin/python}
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
export MPICH_OFI_NIC_POLICY=ROUND-ROBIN
unset LCT_PMI_BACKEND ACIC_SRUN_MPI
printf 'job=%s hosts=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST"
sha256sum "$ROOT"/bin/{riken_sssp,gap_sssp,gluon_sssp}
cat "$ROOT/bin/baselines-manifest.txt" 2>/dev/null || true
module list 2>&1
"$PY" -u "$APP/benchmarks/run.py" "$ROOT" "$@"
echo "COMPARE COMPLETE $SLURM_JOB_ID"
