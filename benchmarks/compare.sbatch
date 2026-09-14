#!/usr/bin/env bash
# rfp-delta-cpu, not mzu-delta-cpu. On 2026-09-14 the mzu account's fairshare
# was spent -- EffectvUsage 1.000000, FairShare 0.0107 -- which put the
# fairshare term of our priority at 107 of 240 and left three queued jobs
# behind 97 others with a backfill estimate a day out. The same three jobs
# resubmitted on rfp-delta-cpu, whose allocation was unused, came out at
# priority 10110 with nothing ahead of them. Override with
# `sbatch --account=...` if the balance shifts back.
#SBATCH --account=rfp-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-compare
set -euo pipefail
ulimit -c 0
APP=${ACIC_BENCH_APP:-/u/rao1/charm_graph_code}
printf 'job=%s hosts=%s\n' "$SLURM_JOB_ID" "$SLURM_JOB_NODELIST"
sha256sum "$1/bin/acic" "$1/bin/riken_sssp" "$1/bin/gap_sssp"
lscpu
python3 -u "$APP/benchmarks/run.py" "$@"
