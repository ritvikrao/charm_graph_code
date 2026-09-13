#!/usr/bin/env bash
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
DEPS=${ACIC_BENCH_DEPS:-$HOME/acic-comparison-deps}
SOURCE=$DEPS/galois
BUILD=$DEPS/galois-build
# Pin this source revision; download a shallow checkout before building.
[ "$(git -C "$SOURCE" rev-parse HEAD)" = b67f94206a8c47fd414446621f6633a31c49fd98 ]
if ! rg -q 'bench::print' "$SOURCE/lonestar/analytics/distributed/sssp/sssp_push.cpp"; then
  git -C "$SOURCE" apply "$APP/benchmarks/gluon.patch"
fi
# Use installed LLVM as a library; loading its module replaces the GNU compiler
# and disables Cray MPI. The conda Boost installation includes serialization.
cmake -S "$SOURCE" -B "$BUILD" -DGALOIS_ENABLE_DIST=ON \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cc -DCMAKE_CXX_COMPILER=CC \
  -DUSE_ARCH=znver3 -DACIC_BENCH_INCLUDE="$APP/benchmarks" \
  -DLLVM_DIR=/sw/rh9.4/spack/v1.0.0/sw/linux-x86_64_v2/llvm-19.1.7-zbfboml/lib/cmake/llvm \
  -DBoost_DIR=/sw/external/python/anaconda3_cpu/lib/cmake/Boost-1.73.0 \
  -DBOOST_ROOT=/sw/external/python/anaconda3_cpu
cmake --build "$BUILD" --target sssp-push-dist --parallel 2
cp "$BUILD/lonestar/analytics/distributed/sssp/sssp-push-dist" "$DEPS/bin/gluon_sssp"
