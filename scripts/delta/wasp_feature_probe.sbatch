#!/usr/bin/env bash
# Compile and run artifact-provided Wasp SSSP feature variants on Delta.
# Usage: sbatch --output=CAMPAIGN/wasp-features-%j.out wasp_feature_probe.sbatch CAMPAIGN
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --job-name=wasp-features
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign directory}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
WASP=$HOME/acic-comparison-deps/wasp-ae/wasp-ae/impl/wasp
mkdir -p "$ROOT/bin"
g++ -O3 -DNDEBUG -std=c++17 -march=znver3 -Wno-interference-size -fopenmp \
  -I"$WASP/include" -I"$WASP/src" -I"$APP/benchmarks" \
  "$APP/benchmarks/wasp_feature_driver.cpp" -o "$ROOT/bin/wasp_feature" -lnuma
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
python3 "$APP/benchmarks/wasp_feature_probe.py" "$ROOT"
