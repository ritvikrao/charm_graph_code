#!/usr/bin/env bash
# Isolated builds: production, D0, and the frozen pre-plan revision.
# Usage: build_onenode.sh CAMPAIGN LABEL [REVISION|working] [diagnostic]
# WIRE=compact64 builds the 12-byte wire for graphs past 2^31 vertices
# (default: the Makefile's, compact).
set -euo pipefail
ROOT=${1:?campaign directory}
LABEL=${2:?binary label}
REV=${3:-working}
KIND=${4:-production}
APP=$(cd "$(dirname "$0")/../.." && pwd)
HTRAM=${HTRAM:-$HOME/htram}
CHARMC=${CHARMC:-$HOME/charm_reconverse/bin/charmc}
if [ -e "$ROOT/bin/$LABEL" ] || [ -e "$ROOT/bin/$LABEL.manifest" ]; then
  echo "Refusing to overwrite frozen build $LABEL; choose a new label" >&2
  exit 2
fi
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
python3 - "$WORK" <<'PY'
import hashlib, sys
from pathlib import Path
root = Path(sys.argv[1])
with (root/'source-files.sha256').open('w') as output:
    for directory in ['src', 'htram']:
        for path in sorted((root/directory).rglob('*')):
            if path.is_file():
                digest = hashlib.sha256()
                with path.open('rb') as stream:
                    for chunk in iter(lambda: stream.read(1048576), b''):
                        digest.update(chunk)
                output.write(f'{digest.hexdigest()}  {path.relative_to(root)}\n')
PY
# htram's object recipe does not use OPTS. Put the shared defines on charmc
# itself so the application, wire packing, and transport timers agree.
FLAGS=
TARGET=sssp_smp
if [ "$KIND" = diagnostic ]; then
  FLAGS='-DACIC_IPDPS_DIAG -DACIC_DIAG -DACIC_COMM_SHARE -DVCOUNT'
elif [ "$KIND" = work-cost ]; then
  FLAGS='-DACIC_WORK_COST -DACIC_COMM_SHARE'
elif [ "$KIND" = papi ]; then
  # Solve-window PC sampler (acic_prof.h); timer mode needs no PMU events.
  TARGET=sssp_smp_papi
elif [ "$KIND" = projections ]; then
  TARGET=sssp_smp_projections
elif [ "$KIND" != production ]; then
  echo "Unknown build kind: $KIND" >&2
  exit 2
fi
make -C "$WORK/src" "$TARGET" CHARMC_SMP="$CHARMC $FLAGS" HTRAM_DIR="$WORK/htram" ${PAPI_HOME:+PAPI_HOME=$PAPI_HOME} ${WIRE:+WIRE=$WIRE} \
  > "$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log"; exit 1; }
install -m755 "$WORK/src/$TARGET" "$ROOT/bin/$LABEL"
{
  echo "revision=$REV kind=$KIND build=$WORK"
  echo "compiler=$CHARMC flags=$FLAGS wire=${WIRE:-default}"
  g++ --version | head -n 1
  git -C "$APP" rev-parse HEAD
  git -C "$HTRAM" rev-parse HEAD
  git -C "$APP" diff --stat
  sha256sum "$ROOT/bin/$LABEL" "$WORK/source-files.sha256" "$WORK/src/sssp_smp.cpp" "$WORK/src/weighted_node_struct.h"
} > "$ROOT/bin/$LABEL.manifest"
echo "Built $ROOT/bin/$LABEL"
