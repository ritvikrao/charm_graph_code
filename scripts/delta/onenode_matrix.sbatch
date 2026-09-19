#!/usr/bin/env bash
# Run once in each of two allocations at 1/2/8 nodes. L1 uses tuning sources
# for the tile sweep; later lever comparisons use four held-out sources.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --job-name=acic-gap-matrix
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
STEP=${2:?L1 or L2 or L3}
GRAPHS=${3:-mesh24-z,mesh26-z,road-usa-z}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
WORKERS=${WORKERS:-120}
RPN=${RPN:-8}
IFS=, read -r -a graphs <<< "$GRAPHS"
for graph in "${graphs[@]}"; do
  variants=$ROOT/logs/$STEP-$graph-$SLURM_JOB_ID.variants.json
  python3 - "$ROOT" "$STEP" "$graph" "$((SLURM_NNODES * WORKERS))" "$variants" <<'PY'
import json, struct, sys
from pathlib import Path
root, step, graph, pes, output = sys.argv[1:]
pes = int(pes)
variants = [dict(label='frozen', binary='acic_frozen'), dict(label='control', binary='acic_frozen')]
if step == 'L1':
    with open(Path(root)/'graphs'/f'{graph}.wsg', 'rb') as stream:
        n = struct.unpack('=Bqq', stream.read(17))[2]
    for pieces in [1, 4, 16, 64]:
        tile = max(1, n // (pieces * pes))
        variants.append(dict(label=f'tiles{pieces}', binary='acic_frozen', graph=f'{graph}-p{pes}-t{tile}'))
elif step == 'L2':
    variants.append(dict(label='shared', binary='acic_L2_final', flags=['--process-share', 'on']))
elif step == 'L3':
    variants += [dict(label='shared', binary='acic_L3', flags=['--process-share', 'on']),
                 dict(label='slack', binary='acic_L3', flags=['--process-share', 'on', '--slack-control', 'on'])]
else:
    raise SystemExit('unknown step')
Path(output).write_text(json.dumps(variants, indent=2))
PY
  if [ "$STEP" = L1 ]; then
    selection=(--source-role tune --sources 2 --reps 2)
  else
    selection=(--sources 4 --reps 3)
  fi
  python3 "$APP/benchmarks/onenode_ab.py" "$ROOT" "$graph" "$variants" \
    --workers "$WORKERS" --rpn "$RPN" "${selection[@]}"
done
