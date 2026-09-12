#!/usr/bin/env bash
#
# Graph generation must be bit-identical on every machine in the evaluation,
# or "the same graph" means different things on Frontier, Vista, and a laptop,
# and no cross-machine number is comparable. It also underpins the artifact
# appendix: a reader must be able to regenerate our exact inputs.
#
# This compiles tools/graph_digest.cpp with every C++ toolchain it can find and
# checks they all agree. It needs no Charm++ build, so it runs anywhere.
#
# The failure it exists to catch is real: <random> is only reproducible within
# one standard library. libc++ and libstdc++ returned different graphs from the
# same seed, which broke CI on 2026-09-10 while passing locally.
#
set -uo pipefail
cd "$(dirname "$0")/.."

# <vertices> <edges|average degree> <seed> <mode> <source>
# mode 1 uniform and 2 mesh take an average degree; mode 3 rmat takes an edge
# count and needs a power-of-two vertex count.
CONFIGS=(
  "10000 16 1 1 1"
  "10000 16 7 1 4242"
  "50000 16 1 1 1"
  "200000 8 5 1 0"
  "10000 16 1 2 0"
  "40000 16 3 2 0"
  "16384 262144 1 3 0"
  "16384 262144 5 3 9"
  "65536 1048576 1 3 42"
)

SDK=""
if [ "$(uname)" = "Darwin" ]; then
  SDK="-isysroot $(xcrun --show-sdk-path)"
fi

BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT

compilers=()
for candidate in c++ clang++ g++ g++-15 g++-14 g++-13; do
  command -v "$candidate" >/dev/null 2>&1 || continue
  # shellcheck disable=SC2086
  if "$candidate" -O2 -std=c++17 -DGRAPH_GEN_STANDALONE $SDK -I. \
      tools/graph_digest.cpp -o "$BUILD_DIR/$candidate" 2>/dev/null &&
     "$candidate" -O2 -std=c++17 -DGRAPH_GEN_STANDALONE $SDK -I. \
      tools/graph_convert.cpp -o "$BUILD_DIR/$candidate.convert" 2>/dev/null; then
    compilers+=("$candidate")
  fi
done

if [ ${#compilers[@]} -eq 0 ]; then
  echo "No usable C++ compiler found"
  exit 1
fi
echo "Toolchains: ${compilers[*]}"

# Distinct standard libraries are what actually make this test meaningful; one
# compiler can only prove the tool builds.
if [ ${#compilers[@]} -lt 2 ]; then
  echo "WARNING: only one toolchain available, so nothing is being compared."
  echo "         Install a second (e.g. g++ alongside clang++) for real coverage."
fi

failures=0
for cfg in "${CONFIGS[@]}"; do
  reference=""
  reference_compiler=""
  for compiler in "${compilers[@]}"; do
    # shellcheck disable=SC2086
    digest=$("$BUILD_DIR/$compiler" $cfg)
    if [ -z "$reference" ]; then
      reference="$digest"
      reference_compiler="$compiler"
    elif [ "$digest" != "$reference" ]; then
      echo "MISMATCH [$cfg]"
      echo "    $reference_compiler: $reference"
      echo "    $compiler: $digest"
      failures=$((failures + 1))
    fi
  done
  [ $failures -eq 0 ] && echo "  ok [$cfg] $reference"
done

# The serialized-graph writer has to be portable for the same reason the
# generator does: the artifact appendix says a reader can regenerate our inputs,
# and a .wsg that differs between toolchains would make that false.
for compiler in "${compilers[@]}"; do
  "$BUILD_DIR/$compiler.convert" gen 3 16384 262144 1 "$BUILD_DIR/$compiler.wsg" \
      > /dev/null || { echo "convert failed under $compiler"; failures=$((failures + 1)); }
done
reference_file=""
for compiler in "${compilers[@]}"; do
  [ -f "$BUILD_DIR/$compiler.wsg" ] || continue
  if [ -z "$reference_file" ]; then
    reference_file="$BUILD_DIR/$compiler.wsg"
  elif ! cmp -s "$reference_file" "$BUILD_DIR/$compiler.wsg"; then
    echo "MISMATCH: .wsg written under $compiler differs byte for byte"
    failures=$((failures + 1))
  fi
done
[ -n "$reference_file" ] && echo "  ok [.wsg writer] $(wc -c < "$reference_file" | tr -d ' ') bytes, identical across toolchains"

if [ $failures -ne 0 ]; then
  echo "GENERATOR IS NOT PORTABLE ($failures mismatches)"
  exit 1
fi
echo "GENERATOR PORTABILITY OK (${#CONFIGS[@]} configurations x ${#compilers[@]} toolchains)"
