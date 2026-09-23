#!/usr/bin/env bash
# Frontier build of the graph tools and the external SSSP baselines: GAPBS
# (plus its work-cost diagnostic build), RIKEN Graph500-SSSP (plus the
# RELAX_SENT counting build and the MPI-share preload), Gluon (Galois
# distributed sssp_push) and the Wasp SC25 artifact with a digest adapter.
# Same revisions, adapters, patches and flags as
# benchmarks/build.sh, benchmarks/build_gluon.sh and scripts/anvil/build_baselines.sh.
# What differs on Frontier:
#
# * One toolchain for every system, the one charm_reconverse is built with
#   here: PrgEnv-gnu 8.6.0, gcc-native 13.2, Cray MPICH 8.1.31 through the
#   cc/CC wrappers (Delta: GCC 14 + Cray MPICH; Anvil: GCC 11.2 + OpenMPI).
# * Boost 1.85.0 comes from the gcc-13.2 spack module. There is no LLVM with
#   RTTI or fmt module, so, as on Anvil, LLVMSupport 19.1.7 (RTTI on) and fmt
#   10.2.1 are built from release tarballs into $DEPS/prefix.
# * cray-libsci is unloaded, as on Delta: SSSP uses no BLAS, and LibSci's
#   exit-time profiling crashed Gluon at shutdown (Delta job 22033720).
# * EPYC 7A53 (Trento) is Zen 3, so Galois keeps USE_ARCH=znver3.
#
# Solver kernels are unchanged; see benchmarks/README.md. Each stage is skipped
# if its output exists, so a rerun resumes. Run on a login node.
#
#   ACIC_BENCH_DEPS=/lustre/orion/csc710/scratch/$USER/acic/deps \
#     bash scripts/frontier/build_baselines.sh
set -euo pipefail
APP=$(cd "$(dirname "$0")/../.." && pwd)
DEPS=${ACIC_BENCH_DEPS:-/lustre/orion/csc710/scratch/$USER/acic/deps}
OUT=$DEPS/bin
PREFIX=$DEPS/prefix
JOBS=${JOBS:-16}
# Never pipe `module`: it is a shell function, and a pipe runs it in a subshell.
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1 || module load PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 cmake/3.31.11 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
export PATH=$(echo "$PATH" | tr : '\n' | grep -v "$HOME/.local/bin" | paste -sd:)
[ "$(g++ -dumpversion | cut -d. -f1)" = 13 ] || { echo "expected gcc 13, got $(g++ -dumpversion)" >&2; exit 1; }
[ -n "${BOOST_ROOT:-}" ] || { echo "boost module did not set BOOST_ROOT" >&2; exit 1; }
# System python3 is 3.6; the helper scripts need 3.7+ (and numpy/scipy later).
PY=${ACIC_PYTHON:-$(dirname "$DEPS")/venv/bin/python}
mkdir -p "$OUT" "$PREFIX"
RIKEN=$DEPS/riken-552f156
GAP=$DEPS/gapbs
GALOIS=$DEPS/galois

[ "$(git -C "$GAP" rev-parse HEAD)" = 2972aeb2703165bafd921222f4ed7196f542d3a8 ]
[ "$(git -C "$DEPS/Graph500-SSSP" rev-parse HEAD)" = 552f156297d921856b74f5c238a93bcbd361bb95 ]
[ "$(git -C "$GALOIS" rev-parse HEAD)" = b67f94206a8c47fd414446621f6633a31c49fd98 ]

# --- Graph tools (no Charm++): input preparation, independent references,
# and graphlib's digest/convert tools.
g++ -O3 -std=c++17 -DGRAPH_GEN_STANDALONE -I"$APP" -I"$APP/benchmarks" \
  "$APP/benchmarks/prepare_graph.cpp" -o "$OUT/prepare_graph"
g++ -O3 -std=c++17 -fopenmp -I"$GAP/src" -I"$APP/benchmarks" \
  "$APP/benchmarks/reference_gap.cpp" -o "$OUT/reference_gap"
make -C "$APP" tools CXX=g++ >/dev/null
cp "$APP/graph_digest" "$APP/graph_convert" "$OUT/"
echo "BUILT prepare_graph reference_gap graph_digest graph_convert"

# --- GAPBS: the production driver and the R0 work-cost build (a private,
# counted copy of sssp.cc; the upstream checkout is not modified).
g++ -O3 -std=c++17 -fopenmp -I"$GAP/src" -I"$APP/benchmarks" \
  "$APP/benchmarks/gap_driver.cpp" -o "$OUT/gap_sssp"
if [ ! -x "$DEPS/gap-work-cost/gap_work_cost" ]; then
  rm -rf "$DEPS/gap-work-cost"
  "$PY" "$APP/benchmarks/build_gap_work_cost.py" "$GAP" "$DEPS/gap-work-cost"
fi
cp "$DEPS/gap-work-cost/gap_work_cost" "$OUT/gap_work_cost"
echo "BUILT gap_sssp gap_work_cost"

# --- RIKEN: same sources, patches and flags as benchmarks/build.sh.
if [ ! -d "$RIKEN" ]; then
  mkdir "$RIKEN"
  git -C "$DEPS/Graph500-SSSP" archive 552f156297d921856b74f5c238a93bcbd361bb95 | tar -x -C "$RIKEN"
  sed -i 's/#include "limits.h"/#include <limits>/' "$RIKEN/src/sssp/graph_constructor.hpp"
fi
# Upstream checks the OS page size at startup; Frontier x86 uses 4096 bytes.
sed -i 's/^#define PAGE_SIZE 8192/#define PAGE_SIZE 4096/' "$RIKEN/src/utils/parameters.h"
[ "$(getconf PAGESIZE)" = 4096 ]
RIKEN_FLAGS=(-O3 -std=c++17 -fopenmp -pthread -msse4.2 -DNDEBUG -include cinttypes
  -Drestrict=__restrict__ -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_FORMAT_MACROS
  -DVERTEX_REORDERING=2)
cc -O3 -I"$RIKEN/src/generator" -c "$RIKEN/src/generator/splittable_mrg.c" -o "$OUT/mrg.o"
CC "${RIKEN_FLAGS[@]}" -DVERBOSE_MODE=0 \
  -I"$RIKEN/src/utils" -I"$RIKEN/src/sssp" -I"$RIKEN/src/generator" -I"$APP/benchmarks" \
  "$APP/benchmarks/riken_driver.cpp" "$RIKEN/src/sssp/low_level_func.cc" "$OUT/mrg.o" \
  -o "$OUT/riken_sssp"
# Step 8a: RIKEN counting its relaxations (RELAX_SENT). Never timed.
RCOUNT=$DEPS/riken-count
if [ ! -d "$RCOUNT" ]; then
  cp -r "$RIKEN" "$RCOUNT"
  patch -d "$RCOUNT" -p1 < "$APP/benchmarks/riken_count.patch"
fi
CC "${RIKEN_FLAGS[@]}" -DVERBOSE_MODE=1 \
  -I"$RCOUNT/src/utils" -I"$RCOUNT/src/sssp" -I"$RCOUNT/src/generator" -I"$APP/benchmarks" \
  "$APP/benchmarks/riken_driver.cpp" "$RCOUNT/src/sssp/low_level_func.cc" "$OUT/mrg.o" \
  -o "$OUT/riken_sssp_verbose"
# 7.6o: MPI time share around the solve, LD_PRELOADed under riken_sssp.
cc -O2 -shared -fPIC "$APP/benchmarks/mpi_share.c" -o "$OUT/mpi_share.so"
echo "BUILT riken_sssp riken_sssp_verbose mpi_share.so"

# --- fmt.
if [ ! -f "$PREFIX/lib64/cmake/fmt/fmt-config.cmake" ]; then
  rm -rf "$DEPS/fmt-10.2.1" "$DEPS/fmt-build"
  tar -xzf "$DEPS/fmt-10.2.1.tar.gz" -C "$DEPS"
  cmake -S "$DEPS/fmt-10.2.1" -B "$DEPS/fmt-build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
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
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
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
cmake -S "$GALOIS" -B "$DEPS/galois-build" -U 'MPI_*' -DGALOIS_ENABLE_DIST=ON \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cc -DCMAKE_CXX_COMPILER=CC \
  -DUSE_ARCH=znver3 -DACIC_BENCH_INCLUDE="$APP/benchmarks" \
  -DLLVM_DIR="$PREFIX/lib/cmake/llvm" -Dfmt_DIR="$PREFIX/lib64/cmake/fmt" \
  -DBOOST_ROOT="$BOOST_ROOT" -DBoost_NO_BOOST_CMAKE=ON
cmake --build "$DEPS/galois-build" --target sssp-push-dist --parallel "$JOBS"
cp "$DEPS/galois-build/lonestar/analytics/distributed/sssp/sssp-push-dist" "$OUT/gluon_sssp"
echo "BUILT gluon_sssp"

# --- Wasp (SC25 artifact, zenodo 15872863, $DEPS/wasp-ae.zip): the artifact's
# own sssp with its serial verifier, and bin/wasp_sssp, which replaces only its
# main (benchmarks/wasp_driver.cpp) to print the harness BENCH digest. The only
# source change is -march=native -> znver3 in the artifact's CMakeLists.txt,
# so the build does not depend on the login node's CPU.
WASP=$DEPS/wasp-ae/wasp-ae/impl/wasp
[ -d "$WASP" ] || unzip -q "$DEPS/wasp-ae.zip" -d "$DEPS/wasp-ae"
sed -i 's/-march=native/-march=znver3/' "$WASP/CMakeLists.txt"
cmake -S "$WASP" -B "$WASP/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build "$WASP/build" --target sssp --parallel "$JOBS"
g++ -O3 -DNDEBUG -std=c++17 -march=znver3 -Wno-interference-size -fopenmp \
  -I"$WASP/include" -I"$WASP/src" -I"$APP/benchmarks" \
  "$APP/benchmarks/wasp_driver.cpp" -o "$OUT/wasp_sssp" -lnuma
echo "BUILT wasp_sssp"

{
  printf 'baselines built %s on %s\n' "$(date -Is)" "$(hostname)"
  printf 'app '; git -C "$APP" rev-parse HEAD
  git -C "$APP" status --short
  printf 'gapbs '; git -C "$GAP" rev-parse HEAD
  printf 'riken 552f156297d921856b74f5c238a93bcbd361bb95\n'
  printf 'galois '; git -C "$GALOIS" rev-parse HEAD
  printf 'llvm 19.1.7 '; sha256sum "$DEPS/llvm-project-19.1.7.src.tar.xz" | cut -d' ' -f1
  printf 'fmt 10.2.1 '; sha256sum "$DEPS/fmt-10.2.1.tar.gz" | cut -d' ' -f1
  printf 'wasp-ae zenodo 15872863 '; sha256sum "$DEPS/wasp-ae.zip" | cut -d' ' -f1
  printf 'boost %s\n' "$BOOST_ROOT"
  g++ --version | head -1
  CC --version | head -1
  module list 2>&1
  ldd "$OUT/riken_sssp" "$OUT/gluon_sssp" | grep -E 'mpi|fabric|boost|numa|sci'
  sha256sum "$OUT"/{prepare_graph,reference_gap,graph_digest,graph_convert,gap_sssp,gap_work_cost} \
    "$OUT"/{riken_sssp,riken_sssp_verbose,mpi_share.so,gluon_sssp,wasp_sssp} \
    "$APP/benchmarks/"{prepare_graph,reference_gap,gap_driver,riken_driver,wasp_driver}.cpp \
    "$APP/benchmarks/common.h" "$APP/benchmarks/gluon.patch" "$APP/benchmarks/riken_count.patch"
} > "$OUT/baselines-manifest.txt"
echo "BASELINES COMPLETE"
