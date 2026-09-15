#!/usr/bin/env bash
# Galois .gr copies of the canonical .wsg inputs, for the Gluon arms of 7.6f1.
# benchmarks/to_galois.py preserves every vertex, edge and integer weight; the
# measured runs' digests check the conversion. rmat26 writes ~17 GB.
#
#   sbatch scripts/anvil/prepare_gluon.sbatch /anvil/scratch/$USER/acic/campaign \
#       mesh24,mesh26,rmat25,rmat26,uniform25,road-usa,orkut
#SBATCH --account=asc050025
#SBATCH --partition=shared
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-gluon-inputs
set -uo pipefail
module load python/3.9.5 >/dev/null 2>&1
CAMPAIGN=${1:?campaign directory}
GRAPHS=${2:?comma-separated graph names}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
cd "$CAMPAIGN/graphs"
# Converting rmat20 (250 MB) took about a second on a login node; rmat26 is
# 70x that, so the hour is mostly margin for Lustre write contention. A .gr
# without its .done marker is from a killed conversion and is redone.
for g in ${GRAPHS//,/ }; do
  [ -e "$g.gr.done" ] && continue
  ( python3 "$APP/benchmarks/to_galois.py" "$g.wsg" && touch "$g.gr.done" ||
      echo "CONVERSION FAILED $g" ) &
done
wait
for g in ${GRAPHS//,/ }; do ls -la "$g.gr" "$g.gr.done" || exit 1; done
printf 'GLUON INPUTS COMPLETE\n'
