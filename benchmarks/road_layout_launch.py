#!/usr/bin/env python3
"""Explicit Delta worker placement, including compact reduced-core layouts."""
import json
import os
import sys


def core_maps(layout):
    ranks, ppn, span = (layout[k] for k in ('ranks', 'ppn', 'span'))
    assert ranks > 0 and span in (16, 32, 64, 128) and span % ranks == 0
    stride = span // ranks
    assert 1 <= ppn < stride  # Preserve at least one unused core per rank's region.
    result = [list(range(rank*stride, rank*stride+ppn)) for rank in range(ranks)]
    assert len(set(sum(result, []))) == ranks*ppn
    return result


if __name__ == '__main__':
    layout = json.loads(sys.argv[1])
    rank = int(os.environ['SLURM_LOCALID'])
    cores = core_maps(layout)[rank]
    allowed = os.sched_getaffinity(0)
    assert set(cores) <= allowed, (rank, cores, sorted(allowed))
    print('ROAD_PLACEMENT '+json.dumps(dict(rank=rank, cores=cores,
          initial_allowed=sorted(allowed))), flush=True)
    os.execv(sys.argv[2], sys.argv[2:]+['+ppn', str(layout['ppn']),
             '+pemap', ','.join(map(str, cores)), '+showcpuaffinity', '+lci_ndevices', '4'])
