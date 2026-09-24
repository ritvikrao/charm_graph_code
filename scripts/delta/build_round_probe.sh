#!/usr/bin/env bash
# Reproduce the road attribution binaries with one explicit, already-built
# Charm++/Reconverse runtime. Frozen names cannot be overwritten.
# Usage: build_round_probe.sh CAMPAIGN CHARM_BUILD [SOURCE_REVISION]
set -euo pipefail
ROOT=${1:?campaign}
TREE=${2:?Charm++ build directory}
REV=${3:-working}
APP=$(cd "$(dirname "$0")/../.." && pwd)
export CHARMC=$TREE/bin/charmc
for spec in acic_latest:production acic_quiet_rounds:quiet-rounds \
            acic_round_profile_v2:round-profile acic_cost:work-cost \
            acic_round_trace:projections; do
  bash "$APP/scripts/delta/build_onenode.sh" "$ROOT" "${spec%:*}" "$REV" "${spec#*:}"
done
if [ -e "$ROOT/bin/round_trip" ]; then
  echo 'Refusing to overwrite round_trip' >&2
  exit 2
fi
WORK=$(mktemp -d "$ROOT/build/round-trip.XXXXXX")
if [ "$REV" = working ]; then
  cp "$APP/benchmarks/round_trip.cpp" "$APP/benchmarks/round_trip.ci" "$WORK/"
else
  git -C "$APP" show "$REV:benchmarks/round_trip.cpp" > "$WORK/round_trip.cpp"
  git -C "$APP" show "$REV:benchmarks/round_trip.ci" > "$WORK/round_trip.ci"
fi
cd "$WORK"
"$CHARMC" round_trip.ci
"$CHARMC" -O3 -g -language charm++ -o "$ROOT/bin/round_trip" round_trip.cpp
sha256sum "$ROOT/bin/round_trip" round_trip.cpp round_trip.ci > "$ROOT/bin/round_trip.manifest"
