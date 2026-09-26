#!/usr/bin/env bash
# Build and run the bounded one-node contribution-placement screen.
# Usage: sbatch ... road_contribution.sbatch CAMPAIGN SOURCE_CAMPAIGN REVISION
# The graph campaign is read-only; binaries, manifests and logs go on HDD.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=acic-road-contrib
set -euo pipefail
ulimit -c 0
ROOT=${1:?new campaign directory}
SOURCE=${2:?campaign containing road-usa-z and mesh24-z}
REV=${3:?application revision}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
mkdir -p "$ROOT/bin" "$ROOT/build" "$ROOT/graphs" "$ROOT/logs" "$ROOT/protocol"

for graph in road-usa-z mesh24-z; do
  for suffix in .wsg .meta .perm .reference.txt; do
    source_path=$SOURCE/graphs/$graph$suffix
    target_path=$ROOT/graphs/$graph$suffix
    test -f "$source_path"
    if [ -e "$target_path" ] || [ -L "$target_path" ]; then
      test "$(readlink -f "$target_path")" = "$(readlink -f "$source_path")"
    else
      ln -s "$source_path" "$target_path"
    fi
  done
done

if [ ! -e "$ROOT/bin/acic_contrib_immediate" ]; then
  bash "$APP/scripts/delta/build_onenode.sh" "$ROOT" acic_contrib_immediate "$REV" production
fi
if [ ! -e "$ROOT/bin/acic_contrib_post_work" ]; then
  ACIC_BUILD_FLAGS=-DACIC_POST_WORK_CONTRIBUTION \
    bash "$APP/scripts/delta/build_onenode.sh" "$ROOT" acic_contrib_post_work "$REV" production
fi
grep -q "^revision=$REV kind=production " "$ROOT/bin/acic_contrib_immediate.manifest"
grep -q "^revision=$REV kind=production " "$ROOT/bin/acic_contrib_post_work.manifest"
grep -q '^compiler=.* flags= wire=' "$ROOT/bin/acic_contrib_immediate.manifest"
grep -q ' flags=-DACIC_POST_WORK_CONTRIBUTION wire=' "$ROOT/bin/acic_contrib_post_work.manifest"

BUILD=$(sed -n 's/.* build=//p' "$ROOT/bin/acic_contrib_post_work.manifest")
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
export LCT_PMI_BACKEND=local
export SSSP_MAKE_ARGS="CHARMC_SMP=$HOME/charm_reconverse/bin/charmc HTRAM_DIR=$BUILD/htram"
(cd "$BUILD/src"; bash scripts/verify.sh) > "$ROOT/logs/post-work-unit-$SLURM_JOB_ID.out" 2>&1
unset LCT_PMI_BACKEND SSSP_MAKE_ARGS

python3 "$APP/benchmarks/road_contribution_study.py" "$ROOT"
