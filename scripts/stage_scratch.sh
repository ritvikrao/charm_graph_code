#!/usr/bin/env bash
#
# Copy both working trees -- uncommitted changes included -- to a fresh
# directory for one batch job, so that the job builds and runs in a tree
# nothing else touches.
#
#   scripts/stage_scratch.sh <dest>    # creates <dest>/charm_graph_code, <dest>/htram
#
set -euo pipefail
DEST="${1:?destination directory}"
APP="$(cd "$(dirname "$0")/.." && pwd)"
HTRAM="${HTRAM_DIR:-$(sed -n 's/^HTRAM_DIR *= *//p' "$APP/config.mk" 2>/dev/null)}"
HTRAM="${HTRAM:-$APP/../htram}"
mkdir -p "$DEST"
EXCLUDES=(--exclude .git --exclude 'core.*' --exclude '*.out' --exclude 'diag-out*' --exclude 'ab-out*' --exclude '.diagnose-build.lock')
rsync -a "${EXCLUDES[@]}" "$APP/" "$DEST/charm_graph_code/"
rsync -a "${EXCLUDES[@]}" "$HTRAM/" "$DEST/htram/"
# The copy must build against its own htram, not the one config.mk names.
CHARMC="$(sed -n 's/^CHARMC_SMP *= *//p' "$APP/config.mk" 2>/dev/null || true)"
{
  [ -n "$CHARMC" ] && echo "CHARMC_SMP = $CHARMC"
  echo "HTRAM_DIR  = $DEST/htram"
} > "$DEST/charm_graph_code/config.mk"
# Build products go, so the job compiles from the sources it was given rather
# than trusting copied timestamps.
rm -f "$DEST"/htram/*.o "$DEST"/htram/*.a "$DEST"/charm_graph_code/*.o \
      "$DEST"/charm_graph_code/sssp_smp "$DEST"/charm_graph_code/sssp_smp_diag
echo "$DEST"
