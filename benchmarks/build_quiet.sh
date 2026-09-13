#!/usr/bin/env bash
# An output-only sensitivity build; preserve the measured primary executable.
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
DEPS=${ACIC_BENCH_DEPS:-$HOME/acic-comparison-deps}
CHARMC=${ACIC_BENCH_CHARMC:-/u/rao1/charm_reconverse/bin/charmc}
HTRAM=${ACIC_BENCH_HTRAM:-$HOME/htram}
python3 - "$APP/sssp_smp.cpp" "$DEPS/sssp_quiet.cpp" <<'PY'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
assert source.count('#define INFO_PRINTS\n') == 1
Path(sys.argv[2]).write_text(source.replace('#define INFO_PRINTS\n', '// INFO_PRINTS disabled for the output-overhead comparison.\n'))
PY
# The primary build creates sssp_smp.{decl,def}.h. Reuse the same htram archive
# and optimization flags; neither relaxation nor controller logic is changed.
cd "$APP"
"$CHARMC" -g -O3 -DTRAM_SMP -DGROUPBY -DGRAPH -DBUCKETS_BY_DEST \
  -DHTRAM_GRAPH_TYPES_HEADER=\""$APP/weighted_node_struct.h"\" \
  -I"$APP" -I"$HTRAM" "$HTRAM/libhtram_group_graph.a" -language charm++ \
  -o "$DEPS/bin/acic_quiet" "$DEPS/sssp_quiet.cpp" -std=c++1z
sha256sum "$DEPS/sssp_quiet.cpp" "$DEPS/bin/acic_quiet"
