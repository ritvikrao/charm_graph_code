#!/usr/bin/env bash
# Isolated builds: production, D0, and the frozen pre-plan revision.
# Usage: build_onenode.sh CAMPAIGN LABEL [REVISION|working] [diagnostic]
set -euo pipefail
ROOT=${1:?campaign directory}
LABEL=${2:?binary label}
REV=${3:-working}
KIND=${4:-production}
APP=$(cd "$(dirname "$0")/../.." && pwd)
HTRAM=${HTRAM:-$HOME/htram}
CHARMC=${CHARMC:-$HOME/charm_reconverse/bin/charmc}
mkdir -p "$ROOT/build" "$ROOT/bin"
WORK=$(mktemp -d "$ROOT/build/$LABEL.XXXXXX")
mkdir "$WORK/src" "$WORK/htram"
if [ "$REV" = working ]; then
  (cd "$APP"; git ls-files -z --cached --others --exclude-standard |
    rsync -a --from0 --files-from=- ./ "$WORK/src/")
else
  git -C "$APP" archive "$REV" | tar -x -C "$WORK/src"
fi
git -C "$HTRAM" ls-files -z | rsync -a --from0 --files-from=- "$HTRAM/" "$WORK/htram/"
# htram's object recipe does not use OPTS. Put the shared defines on charmc
# itself so the application, wire packing, and transport timers agree.
FLAGS=
if [ "$KIND" = diagnostic ]; then
  FLAGS='-DACIC_IPDPS_DIAG -DACIC_DIAG -DACIC_COMM_SHARE -DVCOUNT'
fi
make -C "$WORK/src" sssp_smp CHARMC_SMP="$CHARMC $FLAGS" HTRAM_DIR="$WORK/htram" \
  > "$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log"; exit 1; }
install -m755 "$WORK/src/sssp_smp" "$ROOT/bin/$LABEL"
{
  echo "revision=$REV kind=$KIND build=$WORK"
  git -C "$APP" rev-parse HEAD
  git -C "$HTRAM" rev-parse HEAD
  git -C "$APP" diff --stat
  sha256sum "$ROOT/bin/$LABEL" "$WORK/src/sssp_smp.cpp" "$WORK/src/weighted_node_struct.h"
} > "$ROOT/bin/$LABEL.manifest"
echo "Built $ROOT/bin/$LABEL"
