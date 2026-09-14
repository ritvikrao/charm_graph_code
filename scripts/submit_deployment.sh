#!/usr/bin/env bash
# Submit the item-3 deployment campaign (design/step76-deployment.md).
#
# The transport axis is the reason this is a script and not a command line.
# It compares two binaries, bin/acic_progress and bin/acic_shm, which must
# differ only in which reconverse tree they link; if they straddle a source
# change, the "transport" result is measuring the source change. So this
# builds both from the current tree and stages them in one step, and refuses
# to run while any job of ours is queued -- restaging a binary underneath a
# pending job is what made the earlier 7.6a replay ambiguous.
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
ROOT=${ACIC_BENCH_ROOT:-/scratch/mzu/rao1/acic-comparison-20260913}
SHM_CHARMC=/u/rao1/charm_reconverse/reconverse-linux-x86_64-shm/bin/charmc

pending=$(squeue -u "$USER" -h -o '%i' | wc -l)
if [ "$pending" -ne 0 ]; then
  echo "refusing to restage: $pending job(s) still in the queue" >&2
  squeue -u "$USER" >&2
  exit 2
fi
if [ -n "$(git -C "$APP" status --porcelain -- sssp_smp.cpp sssp_smp.ci)" ]; then
  echo 'refusing to stage an uncommitted solver' >&2
  exit 2
fi

# The default tree first, then the shm tree, from the same source.
make -C "$APP" sssp_smp
cp "$APP/sssp_smp" "$ROOT/bin/acic_progress"
make -C "$APP" -B sssp_smp CHARMC_SMP="$SHM_CHARMC"
cp "$APP/sssp_smp" "$ROOT/bin/acic_shm"
mv "$APP/sssp_smp" "$APP/sssp_smp_shm"
# Leave the working tree holding the default build, as every other script
# expects, rather than whichever transport happened to be built last.
make -C "$APP" -B sssp_smp

{
  printf 'deployment staged %s\n' "$(date -Is)"
  printf 'app '; git -C "$APP" rev-parse HEAD
  sha256sum "$ROOT/bin/acic_progress" "$ROOT/bin/acic_shm" "$APP/sssp_smp.cpp"
} | tee "$ROOT/bin/deployment-manifest.txt"

sbatch --nodes=1 --time=00:45:00 --job-name=acic-deploy \
  --output="$ROOT/logs/deploy-%j.out" \
  "$APP/benchmarks/compare.sbatch" "$ROOT" \
  --mode deployment --graphs mesh22,rmat22,road-ny \
  --workers 120 --sources 4 --reps 3 --timeout 120
