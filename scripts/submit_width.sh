#!/usr/bin/env bash
# Re-take the width A/B across allocations (design/step76-width-repair.md).
#
# Job 22061958 measured the two width rules on one node, and two things about
# that allocation are now known to matter. It ran one process of 120 workers,
# which the item-3 deployment campaign then showed is the worst geometry
# available -- 7x to 20x slower than eight processes of fifteen -- so every
# width number was taken at a layout we would not deploy. And the 7.6b prior
# says the sign of an over-width moves with worker density: the same over-width
# on rmat22 reads 0.98x at 128 workers over eight nodes, 1.00x at 16 workers on
# one, and 1.4x slower at 120 workers on one.
#
# So this fixes the geometry at the deployment candidate (eight processes of
# fifteen per node) and varies only the node count. The one-node job is not a
# repeat: it is the same comparison at the right geometry, and the difference
# between it and 22061958 is the geometry effect on the width result.
#
# Three arms, not five. The freeze question is closed -- it ships on -- so the
# baseline is the shipped default: logv-frozen is that default stated
# explicitly, control is the same thing unflagged, and the gap between them is
# the resolution floor below which no `weight` result is a result.
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
ROOT=${ACIC_BENCH_ROOT:-/scratch/mzu/rao1/acic-comparison-20260913}

pending=$(squeue -u "$USER" -h -o '%i' | wc -l)
if [ "$pending" -ne 0 ]; then
  echo "refusing to restage: $pending job(s) still in the queue" >&2
  squeue -u "$USER" >&2
  exit 2
fi
if [ -n "$(git -C "$APP" status --porcelain -- sssp_smp.cpp sssp_smp.ci benchmarks/run.py)" ]; then
  echo 'refusing to stage an uncommitted solver or harness' >&2
  exit 2
fi

make -C "$APP" sssp_smp
cp "$APP/sssp_smp" "$ROOT/bin/acic_width"
{
  printf 'width staged %s\n' "$(date -Is)"
  printf 'app '; git -C "$APP" rev-parse HEAD
  printf 'htram '; git -C /u/rao1/htram rev-parse HEAD
  sha256sum "$ROOT/bin/acic_width" "$APP/sssp_smp.cpp"
} | tee "$ROOT/bin/width-manifest.txt"

# reps fall from 3 to 2 above two nodes so that the larger allocations finish
# inside the same wall clock; sources and arms are identical throughout, so
# every cell is still a paired median over at least eight runs.
submit() {
  local nodes=$1 reps=$2
  sbatch --nodes="$nodes" --time=02:00:00 --job-name="acic-width-${nodes}n" \
    --output="$ROOT/logs/width-${nodes}n-%j.out" \
    "$APP/benchmarks/compare.sbatch" "$ROOT" \
    --mode width --workers 120 --acic-rpn 8 \
    --arms logv-frozen,weight,control \
    --sources 4 --reps "$reps" --timeout 120
}
submit 1 3
submit 2 3
submit 8 2
submit 16 2
