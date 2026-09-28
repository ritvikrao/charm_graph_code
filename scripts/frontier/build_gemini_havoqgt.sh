#!/usr/bin/env bash
# Frontier build of two more distributed SSSP baselines, with the same
# toolchain as scripts/frontier/build_baselines.sh (PrgEnv-gnu 8.6.0,
# gcc-native 13.2, Cray MPICH 8.1.31 through CC, Boost 1.85.0, no LibSci):
#
# * Gemini (thu-pacman/GeminiGraph 170e7d3), toolkits/sssp with
#   benchmarks/gemini.patch: exact integer weights (uint32) and distances
#   (uint64) in place of float, a list of sources, solve-only timing and the
#   harness BENCH digest; the NUMA layout taken from the rank's cpuset instead
#   of the whole machine; -DGEMINI_VERTEX64 for 64-bit vertex ids (plus int
#   count fixes for |V| > 2^31 and for >2 GiB message buffers); abort unless
#   MPI grants MPI_THREAD_MULTIPLE. Built twice: gemini_sssp (32-bit ids,
#   upstream's width) and gemini_sssp64. Input: wsg_to_gemini.
#   Build flags are upstream's Makefile ones (-O3 -g -Wall -fopenmp, asserts
#   left on: several upstream asserts have side effects) with -march=znver3
#   for -march=native.
# * HavoqGT (LLNL/havoqgt master 2e8b2a8) run_sssp with
#   benchmarks/havoqgt.patch: uint32 weights / uint64 distances in place of
#   double, the exact -s source (upstream skips to the next nonzero-degree
#   vertex), repeated -s, upstream's barrier-to-barrier solve timer, the
#   BENCH digest over every vertex; ingest_edge_list stores uint32 weights;
#   havoqgt_ingest (benchmarks/havoqgt_ingest.cpp) builds the same graph
#   store from a .wsg directly. master persists graphs with Metall; its
#   contemporary v0.10 does not build with Boost >= 1.76 (array_construct
#   left boost::interprocess), so Metall v0.29 (df60411, the first release
#   that supports Boost 1.85) is used, plus one HavoqGT fix it needs:
#   zero-length edge arrays are allocated with one element (Metall v0.29
#   returns nullptr for zero bytes -> bad_alloc when a graph has no hubs).
#   CMake Release flags (-O3 -DNDEBUG) + -std=c++17 -march=znver3.
#
# Solver kernels are unchanged. Each patch is applied once (skipped when
# already applied); rerunning rebuilds. Run on a login node.
#
#   ACIC_BENCH_DEPS=/lustre/orion/csc710/scratch/$USER/acic/deps \
#     bash scripts/frontier/build_gemini_havoqgt.sh
set -euo pipefail
APP=$(cd "$(dirname "$0")/../.." && pwd)
DEPS=${ACIC_BENCH_DEPS:-/lustre/orion/csc710/scratch/$USER/acic/deps}
CAMPAIGN=${ACIC_CAMPAIGN:-$(dirname "$DEPS")/campaign}
OUT=$CAMPAIGN/bin
JOBS=${JOBS:-16}
GEMINI=$DEPS/gemini
HAVOQGT=$DEPS/havoqgt
METALL=$DEPS/metall
# Never pipe `module`: it is a shell function, and a pipe runs it in a subshell.
module swap PrgEnv-cray PrgEnv-gnu/8.6.0 >/dev/null 2>&1 || module load PrgEnv-gnu/8.6.0 >/dev/null 2>&1
module load gcc-native/13.2 cray-mpich/8.1.31 cmake/3.31.11 boost/1.85.0 >/dev/null 2>&1
module unload cray-libsci darshan-runtime >/dev/null 2>&1 || true
export PATH=$(echo "$PATH" | tr : '\n' | grep -v "$HOME/.local/bin" | paste -sd:)
[ "$(g++ -dumpversion | cut -d. -f1)" = 13 ] || { echo "expected gcc 13, got $(g++ -dumpversion)" >&2; exit 1; }
[ -n "${BOOST_ROOT:-}" ] || { echo "boost module did not set BOOST_ROOT" >&2; exit 1; }
mkdir -p "$OUT"

[ -d "$METALL" ] || git clone -q https://github.com/LLNL/metall.git "$METALL"
git -C "$METALL" checkout -q df604114b8eba942785810f553a6006f9967df3f
[ "$(git -C "$GEMINI" rev-parse HEAD)" = 170e7d36794fdcaca077f23bd0cd76e8ccab9e74 ]
[ "$(git -C "$HAVOQGT" rev-parse HEAD)" = 2e8b2a8a0ed764188079f32b1b01b8b24c81e20b ]
[ "$(git -C "$METALL" rev-parse HEAD)" = df604114b8eba942785810f553a6006f9967df3f ]

apply_once() {  # tree patch
  if git -C "$1" apply --reverse --check "$2" 2>/dev/null; then
    echo "already applied: $2"
  else
    git -C "$1" apply "$2"
  fi
}
apply_once "$GEMINI" "$APP/benchmarks/gemini.patch"
apply_once "$HAVOQGT" "$APP/benchmarks/havoqgt.patch"

# --- converter (.wsg -> Gemini binary edge list, or HavoqGT text parts).
g++ -O3 -std=c++17 -march=znver3 -fopenmp -Wall -Wextra \
  "$APP/benchmarks/wsg_to_gemini.cpp" -o "$OUT/wsg_to_gemini"
echo "BUILT wsg_to_gemini"

# --- Gemini.
GEMINI_FLAGS=(-O3 -g -Wall -std=c++17 -fopenmp -march=znver3 -UNDEBUG
  -I"$GEMINI" -I"$APP/benchmarks")
CC "${GEMINI_FLAGS[@]}" "$GEMINI/toolkits/sssp.cpp" -o "$OUT/gemini_sssp" -lnuma
CC "${GEMINI_FLAGS[@]}" -DGEMINI_VERTEX64 "$GEMINI/toolkits/sssp.cpp" -o "$OUT/gemini_sssp64" -lnuma
echo "BUILT gemini_sssp gemini_sssp64"

# --- HavoqGT (+ Metall headers).
cmake -S "$HAVOQGT" -B "$DEPS/havoqgt-build" -Wno-dev -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=cc -DCMAKE_CXX_COMPILER=CC -DCMAKE_CXX_FLAGS="-std=c++17 -march=znver3" \
  -DMETALL_ROOT="$METALL" -DBOOST_ROOT="$BOOST_ROOT" -DHAVOQGT_BUILD_TEST=OFF \
  -DACIC_BENCH_INCLUDE="$APP/benchmarks" >/dev/null
cmake --build "$DEPS/havoqgt-build" --target run_sssp ingest_edge_list havoqgt_ingest --parallel "$JOBS"
cp "$DEPS/havoqgt-build/src/run_sssp" "$OUT/havoqgt_sssp"
cp "$DEPS/havoqgt-build/src/havoqgt_ingest" "$OUT/havoqgt_ingest"
cp "$DEPS/havoqgt-build/src/ingest_edge_list" "$OUT/havoqgt_ingest_edge_list"
echo "BUILT havoqgt_sssp havoqgt_ingest havoqgt_ingest_edge_list"

{
  printf '%s gemini_sssp gemini_sssp64 wsg_to_gemini havoqgt_sssp havoqgt_ingest havoqgt_ingest_edge_list built on %s by scripts/frontier/build_gemini_havoqgt.sh\n' "$(date -Is)" "$(hostname)"
  printf 'app '; git -C "$APP" rev-parse HEAD
  git -C "$APP" status --short
  printf 'gemini %s (thu-pacman/GeminiGraph) + benchmarks/gemini.patch; flags: %s [-DGEMINI_VERTEX64 for gemini_sssp64] -lnuma\n' \
    "$(git -C "$GEMINI" rev-parse HEAD)" "${GEMINI_FLAGS[*]}"
  printf 'havoqgt %s (LLNL/havoqgt master) + benchmarks/havoqgt.patch; CMake Release (-O3 -DNDEBUG) -std=c++17 -march=znver3; build %s\n' \
    "$(git -C "$HAVOQGT" rev-parse HEAD)" "$DEPS/havoqgt-build"
  printf 'metall %s (LLNL/metall %s, headers only)\n' "$(git -C "$METALL" rev-parse HEAD)" "$(git -C "$METALL" describe --tags)"
  printf 'boost %s\n' "$BOOST_ROOT"
  printf 'wsg_to_gemini: g++ -O3 -std=c++17 -march=znver3 -fopenmp\n'
  g++ --version | head -1
  CC --version | head -1
  module list 2>&1 | grep -E '^ +[0-9]+\)' | tr -s ' ' | paste -sd' '
  ldd "$OUT/gemini_sssp" "$OUT/havoqgt_sssp" | grep -E 'mpi|fabric|boost|numa|sci' | sort -u
  sha256sum "$OUT"/{gemini_sssp,gemini_sssp64,wsg_to_gemini,havoqgt_sssp,havoqgt_ingest,havoqgt_ingest_edge_list}
  (cd "$APP" && sha256sum benchmarks/gemini.patch benchmarks/havoqgt.patch benchmarks/havoqgt_ingest.cpp \
    benchmarks/wsg_to_gemini.cpp benchmarks/common.h)
} >> "$OUT/baselines-manifest.txt"
echo "GEMINI HAVOQGT COMPLETE"
