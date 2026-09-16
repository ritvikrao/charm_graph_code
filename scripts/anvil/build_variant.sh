#!/usr/bin/env bash
# Build one ACIC binary for an interleaved A/B (ab_compare.sbatch) from private
# copies of this tree and htram, so variants can build side by side without
# overwriting each other's generated headers or htram's library.
#
#   scripts/anvil/build_variant.sh NAME CHARM_TREE [make variables...]
#
# CHARM_TREE is a Charm++ build directory (the one holding bin/charmc). The
# binary lands in $CAMPAIGN/bin/NAME (default campaign: Anvil scratch), and a
# manifest line records the tree, both source revisions and the variables.
#
#   scripts/anvil/build_variant.sh acic_m2 ~/charm_reconverse/reconverse-linux-x86_64-mpicxx-v0916 \
#     OPTS="-march=znver3"
#
# SRC and HTRAM (environment) override the source trees, for variants that
# need an edited copy.
set -euo pipefail
NAME=${1:?binary name}
TREE=${2:?Charm++ build directory}
shift 2
SRC=${SRC:-$(cd "$(dirname "$0")/../.." && pwd)}
HTRAM=${HTRAM:-$HOME/htram}
CAMPAIGN=${CAMPAIGN:-/anvil/scratch/$USER/acic/campaign}
WORK=/anvil/scratch/$USER/acic/build/variants/$NAME
source /anvil/scratch/$USER/acic/env.sh
rm -rf "$WORK"
mkdir -p "$WORK/src" "$WORK/htram"
# Sources only: generated headers and objects are rebuilt in the copy.
rsync -a --exclude '*.o' --exclude '*.a' --exclude '*.decl.h' --exclude '*.def.h' \
      --exclude 'sssp_smp*' --exclude '.git' --exclude 'slurm-*' \
      "$SRC/" "$WORK/src/"
cp "$SRC/sssp_smp.cpp" "$SRC/sssp_smp.ci" "$WORK/src/"
rsync -a --exclude '*.o' --exclude '*.a' --exclude '*.decl.h' --exclude '*.def.h' \
      --exclude '.git' "$HTRAM/" "$WORK/htram/"
rm -f "$WORK/src/config.mk"
make -C "$WORK/src" sssp_smp CHARMC_SMP="$TREE/bin/charmc" HTRAM_DIR="$WORK/htram" "$@" \
  > "$WORK/build.log" 2>&1 || { tail -n 30 "$WORK/build.log"; exit 1; }
install -m 755 "$WORK/src/sssp_smp" "$CAMPAIGN/bin/$NAME"
rev() {  # rev DIR: short revision, +dirty if edited, or the path for a copy
  if git -C "$1" rev-parse --short HEAD >/dev/null 2>&1; then
    printf '%s%s' "$(git -C "$1" rev-parse --short HEAD)" \
      "$(git -C "$1" diff --quiet || echo +dirty)"
  else
    printf 'copy:%s' "$1"
  fi
}
printf '%s tree=%s src=%s htram=%s vars=%s sha256=%s\n' "$NAME" "$TREE" \
  "$(rev "$SRC")" "$(rev "$HTRAM")" \
  "$*" "$(sha256sum "$CAMPAIGN/bin/$NAME" | cut -c1-16)" >> "$CAMPAIGN/bin/variants-manifest.txt"
echo "built $CAMPAIGN/bin/$NAME"
