#!/usr/bin/env bash
# Anvil build of the external SSSP baselines for 7.6f1: RIKEN Graph500-SSSP,
# GAPBS and Gluon (Galois distributed sssp_push), at the revisions pinned in
# design/step75-data/revisions.json, with the same adapters and flags as the
# Delta build (benchmarks/build.sh, benchmarks/build_gluon.sh). What differs:
#
# * One toolchain for every system, the one ACIC is built with: GCC 11.2 and
#   OpenMPI 4.0.6 (UCX pml, launched by srun --mpi=pmi2) in place of GCC 14 and
#   Cray MPI. gcc/14.2.0 has no OpenMPI build on Anvil.
# * Anvil has no LLVM or fmt module, and Galois needs LLVMSupport (with RTTI)
#   and fmt. Both are built here from release tarballs, LLVM as a distribution
#   of only LLVMSupport/LLVMDemangle, into $DEPS/prefix.
# * Delta's LibSci exclusion does not apply; there is no Cray LibSci.
#
# Solver kernels are unchanged; see benchmarks/README.md. Each stage is skipped
# if its output exists, so a rerun resumes.
#
#   ACIC_BENCH_DEPS=/anvil/scratch/$USER/acic/deps bash scripts/anvil/build_baselines.sh
set -euo pipefail
APP=$(cd "$(dirname "$0")/../.." && pwd)
DEPS=${ACIC_BENCH_DEPS:-/anvil/scratch/$USER/acic/deps}
OUT=$DEPS/bin
PREFIX=$DEPS/prefix
JOBS=${JOBS:-8}
module load gcc/11.2.0 openmpi/4.0.6 boost/1.74.0 numactl/2.0.14 cmake/3.20.0 >/dev/null 2>&1
mkdir -p "$OUT" "$PREFIX"
RIKEN=$DEPS/riken-552f156
GAP=$DEPS/gapbs
GALOIS=$DEPS/galois

[ "$(git -C "$GAP" rev-parse HEAD)" = 2972aeb2703165bafd921222f4ed7196f542d3a8 ]
[ "$(git -C "$DEPS/Graph500-SSSP" rev-parse HEAD)" = 552f156297d921856b74f5c238a93bcbd361bb95 ]
[ "$(git -C "$GALOIS" rev-parse HEAD)" = b67f94206a8c47fd414446621f6633a31c49fd98 ]

# --- RIKEN and GAPBS: same sources, patches and flags as benchmarks/build.sh.
if [ ! -d "$RIKEN" ]; then
  mkdir "$RIKEN"
  git -C "$DEPS/Graph500-SSSP" archive 552f156297d921856b74f5c238a93bcbd361bb95 | tar -x -C "$RIKEN"
  sed -i 's/#include "limits.h"/#include <limits>/' "$RIKEN/src/sssp/graph_constructor.hpp"
fi
sed -i 's/^#define PAGE_SIZE 8192/#define PAGE_SIZE 4096/' "$RIKEN/src/utils/parameters.h"
g++ -O3 -std=c++17 -fopenmp -I"$GAP/src" -I"$APP/benchmarks" \
  "$APP/benchmarks/gap_driver.cpp" -o "$OUT/gap_sssp"
mpicc -O3 -I"$RIKEN/src/generator" -c "$RIKEN/src/generator/splittable_mrg.c" -o "$OUT/mrg.o"
mpicxx -O3 -std=c++17 -fopenmp -pthread -msse4.2 -DNDEBUG -include cinttypes \
  -Drestrict=__restrict__ -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS \
  -D__STDC_FORMAT_MACROS -DVERTEX_REORDERING=2 -DVERBOSE_MODE=0 \
  -I"$RIKEN/src/utils" -I"$RIKEN/src/sssp" -I"$RIKEN/src/generator" -I"$APP/benchmarks" \
  "$APP/benchmarks/riken_driver.cpp" "$RIKEN/src/sssp/low_level_func.cc" "$OUT/mrg.o" \
  -o "$OUT/riken_sssp"
echo "BUILT riken_sssp gap_sssp"

# --- Step 8a: RIKEN counting its relaxations (RELAX_SENT) with its verbose
# phase log on. A measurement build only; never timed against the others.
RCOUNT=$DEPS/riken-count
if [ ! -d "$RCOUNT" ]; then
  cp -r "$RIKEN" "$RCOUNT"
  patch -d "$RCOUNT" -p1 < "$APP/benchmarks/riken_count.patch"
fi
mpicxx -O3 -std=c++17 -fopenmp -pthread -msse4.2 -DNDEBUG -include cinttypes \
  -Drestrict=__restrict__ -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS \
  -D__STDC_FORMAT_MACROS -DVERTEX_REORDERING=2 -DVERBOSE_MODE=1 \
  -I"$RCOUNT/src/utils" -I"$RCOUNT/src/sssp" -I"$RCOUNT/src/generator" -I"$APP/benchmarks" \
  "$APP/benchmarks/riken_driver.cpp" "$RCOUNT/src/sssp/low_level_func.cc" "$OUT/mrg.o" \
  -o "$OUT/riken_sssp_verbose"
echo "BUILT riken_sssp_verbose"

# --- fmt.
if [ ! -f "$PREFIX/lib64/cmake/fmt/fmt-config.cmake" ]; then
  rm -rf "$DEPS/fmt-10.2.1" "$DEPS/fmt-build"
  tar -xzf "$DEPS/fmt-10.2.1.tar.gz" -C "$DEPS"
  cmake -S "$DEPS/fmt-10.2.1" -B "$DEPS/fmt-build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DFMT_TEST=OFF -DFMT_DOC=OFF \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
  cmake --build "$DEPS/fmt-build" --parallel "$JOBS"
  cmake --install "$DEPS/fmt-build"
fi
echo "BUILT fmt"

# --- LLVMSupport with RTTI (Galois refuses an LLVM without it).
if [ ! -f "$PREFIX/lib/cmake/llvm/LLVMConfig.cmake" ]; then
  LLVM_SRC=$DEPS/llvm-project-19.1.7.src
  [ -d "$LLVM_SRC/llvm" ] || tar -xJf "$DEPS/llvm-project-19.1.7.src.tar.xz" -C "$DEPS" \
    llvm-project-19.1.7.src/llvm llvm-project-19.1.7.src/cmake llvm-project-19.1.7.src/third-party
  cmake -S "$LLVM_SRC/llvm" -B "$DEPS/llvm-build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DLLVM_ENABLE_RTTI=ON -DLLVM_TARGETS_TO_BUILD=X86 \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_TOOLS=OFF -DLLVM_BUILD_TOOLS=OFF -DLLVM_INCLUDE_UTILS=OFF \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_TERMINFO=OFF \
    -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
    -DLLVM_DISTRIBUTION_COMPONENTS='LLVMSupport;LLVMDemangle;llvm-headers;cmake-exports'
  cmake --build "$DEPS/llvm-build" --target install-distribution --parallel "$JOBS"
fi
echo "BUILT LLVMSupport"

# --- Gluon: benchmarks/gluon.patch adds only a timer and a result digest.
if ! grep -q 'bench::print' "$GALOIS/lonestar/analytics/distributed/sssp/sssp_push.cpp"; then
  git -C "$GALOIS" apply "$APP/benchmarks/gluon.patch"
fi
cmake -S "$GALOIS" -B "$DEPS/galois-build" -DGALOIS_ENABLE_DIST=ON \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DMPI_C_COMPILER=mpicc -DMPI_CXX_COMPILER=mpicxx \
  -DUSE_ARCH=znver3 -DACIC_BENCH_INCLUDE="$APP/benchmarks" \
  -DLLVM_DIR="$PREFIX/lib/cmake/llvm" -Dfmt_DIR="$PREFIX/lib64/cmake/fmt" \
  -DBOOST_ROOT="$BOOST_ROOT" -DBoost_NO_BOOST_CMAKE=ON
cmake --build "$DEPS/galois-build" --target sssp-push-dist --parallel "$JOBS"
cp "$DEPS/galois-build/lonestar/analytics/distributed/sssp/sssp-push-dist" "$OUT/gluon_sssp"
echo "BUILT gluon_sssp"

{
  printf 'baselines built %s on %s\n' "$(date -Is)" "$(hostname)"
  printf 'app '; git -C "$APP" rev-parse HEAD
  printf 'gapbs '; git -C "$GAP" rev-parse HEAD
  printf 'riken 552f156297d921856b74f5c238a93bcbd361bb95\n'
  printf 'galois '; git -C "$GALOIS" rev-parse HEAD
  printf 'llvm 19.1.7 '; sha256sum "$DEPS/llvm-project-19.1.7.src.tar.xz" | cut -d' ' -f1
  printf 'fmt 10.2.1 '; sha256sum "$DEPS/fmt-10.2.1.tar.gz" | cut -d' ' -f1
  g++ --version | head -1
  mpirun --version 2>&1 | head -1
  module list 2>&1
  ldd "$OUT/riken_sssp" "$OUT/gluon_sssp" | grep -E 'mpi|ucx|boost|numa'
  sha256sum "$OUT/gap_sssp" "$OUT/riken_sssp" "$OUT/gluon_sssp" \
    "$APP/benchmarks/"{gap_driver,riken_driver}.cpp "$APP/benchmarks/common.h" "$APP/benchmarks/gluon.patch"
} > "$OUT/baselines-manifest.txt"
echo "BASELINES COMPLETE"
