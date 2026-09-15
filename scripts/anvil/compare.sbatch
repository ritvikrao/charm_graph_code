#!/usr/bin/env bash
# Anvil counterpart of benchmarks/compare.sbatch, for run.py's ACIC-only modes
# (width, deployment, controller, policy) and the 7.6f1 external re-take
# (external, external_smoke). Anvil CPU nodes have Delta's
# shape -- 128 cores in eight 16-core NUMA domains -- so run.py's 8 x 15 layout
# needs no change; what differs is the launcher (PMI2, which Slurm here does not
# default to) and that the runtime comes from modules.
#
# benchmarks/run.py and benchmarks/launch_acic.sh are read live from APP when
# the job starts. Do not edit either while a job that uses them is queued.
#
#   sbatch -N 2 --time=01:45:00 scripts/anvil/compare.sbatch \
#       /anvil/scratch/$USER/acic/campaign --mode policy --workers 120 --acic-rpn 8
#   sbatch -N 1 --time=01:30:00 scripts/anvil/compare.sbatch \
#       /anvil/scratch/$USER/acic/campaign --mode external --workers 120 --timeout 180 \
#       --sources 4 --reps 1 --graphs mesh24,rmat25,road-usa,orkut
#SBATCH --account=asc050025
#SBATCH --partition=wholenode
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --exclusive
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-compare
set -euo pipefail
ulimit -c 0
module load gcc/11.2.0 openmpi/4.0.6 libfabric/1.12.0 numactl/2.0.14 hwloc/1.11.13 python/3.9.5 boost/1.74.0 >/dev/null 2>&1
export SLURM_MPI_TYPE=pmi2
# The baselines (scripts/anvil/build_baselines.sh) use OpenMPI; insist on its
# UCX path over InfiniBand rather than a silent fall back to TCP.
export OMPI_MCA_pml=ucx
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
printf 'job=%s hosts=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST"
sha256sum "$1/bin/acic" $(ls "$1"/bin/{riken_sssp,gap_sssp,gluon_sssp} 2>/dev/null)
cat "$1/bin/policy-manifest.txt" "$1/bin/baselines-manifest.txt" 2>/dev/null || true
lscpu | grep -E '^CPU\(s\)|NUMA node|Model name'
python3 -u "$APP/benchmarks/run.py" "$@"
