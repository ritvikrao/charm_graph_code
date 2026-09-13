#!/usr/bin/env bash
# Build small adapters; reuse the installed runtime and preserve existing trees.
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
DEPS=${ACIC_BENCH_DEPS:-$HOME/acic-comparison-deps}
OUT=${1:-$DEPS/bin}
mkdir -p "$OUT"
RIKEN=$DEPS/riken-552f156
GAP=$DEPS/gapbs
if [ ! -d "$RIKEN" ]; then
  mkdir "$RIKEN"
  git -C "$HOME/Graph500-SSSP" archive 552f156297d921856b74f5c238a93bcbd361bb95 | tar -x -C "$RIKEN"
  # Compile compatibility fix already present in the user's working checkout.
  sed -i 's/#include "limits.h"/#include <limits>/' "$RIKEN/src/sssp/graph_constructor.hpp"
fi
# Upstream checks the OS page size at startup (Delta x86 uses 4096 bytes).
sed -i 's/^#define PAGE_SIZE 8192/#define PAGE_SIZE 4096/' "$RIKEN/src/utils/parameters.h"
g++ -O3 -std=c++17 -DGRAPH_GEN_STANDALONE -I"$APP" -I"$APP/benchmarks" \
  "$APP/benchmarks/prepare_graph.cpp" -o "$OUT/prepare_graph"
g++ -O3 -std=c++17 -fopenmp -I"$GAP/src" -I"$APP/benchmarks" \
  "$APP/benchmarks/reference_gap.cpp" -o "$OUT/reference_gap"
g++ -O3 -std=c++17 -fopenmp -I"$GAP/src" -I"$APP/benchmarks" \
  "$APP/benchmarks/gap_driver.cpp" -o "$OUT/gap_sssp"
# Use Cray's MPI wrapper, not the unrelated conda mpicxx first in PATH.
cc -O3 -I"$RIKEN/src/generator" -c "$RIKEN/src/generator/splittable_mrg.c" -o "$OUT/mrg.o"
CC -O3 -std=c++17 -fopenmp -pthread -msse4.2 -DNDEBUG -include cinttypes \
  -Drestrict=__restrict__ -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS \
  -D__STDC_FORMAT_MACROS -DVERTEX_REORDERING=2 -DVERBOSE_MODE=0 \
  -I"$RIKEN/src/utils" -I"$RIKEN/src/sssp" -I"$RIKEN/src/generator" -I"$APP/benchmarks" \
  "$APP/benchmarks/riken_driver.cpp" "$RIKEN/src/sssp/low_level_func.cc" "$OUT/mrg.o" \
  -o "$OUT/riken_sssp"
make -C "$APP" sssp_smp
cp "$APP/sssp_smp" "$OUT/acic"
{
  printf 'app '; git -C "$APP" rev-parse HEAD
  printf 'htram '; git -C "$HOME/htram" rev-parse HEAD
  printf 'charm '; git -C "$HOME/charm_reconverse" rev-parse HEAD
  printf 'gapbs '; git -C "$GAP" rev-parse HEAD
  printf 'riken 552f156297d921856b74f5c238a93bcbd361bb95\n'
  g++ --version | head -1
  CC --version | head -1
  module list 2>&1
  sha256sum "$OUT/acic" "$OUT/gap_sssp" "$OUT/riken_sssp" "$APP/sssp_smp.cpp" \
    "$APP/benchmarks/"*.cpp "$APP/benchmarks/common.h" "$APP/benchmarks/run.py"
} > "$OUT/build-manifest.txt"
