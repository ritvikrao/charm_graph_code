#!/usr/bin/env python3
"""Translate canonical .wsg CSR to a Galois .gr without changing edges.

A narrow .wsg (int32 ids) becomes version 1 (32-bit destinations, padded to 8
bytes before the edge data); a wide .wsg (int64 ids, 16-byte edge records,
graphlib/gapbs.h) becomes version 2 (64-bit destinations, no padding), which
only the gluon64.patch build reads. `--v2` writes version 2 from a narrow
file too, as NAME.v2.gr, so both readers can be checked on one graph.
"""
from pathlib import Path
import struct
import sys
import numpy as np

CHUNK = 1 << 24
force_v2 = '--v2' in sys.argv[1:]
for argument in [a for a in sys.argv[1:] if a != '--v2']:
    path = Path(argument)
    with path.open('rb') as f:
        directed, arcs, vertices = struct.unpack('=Bqq', f.read(17))
    if directed or arcs < 0 or vertices <= 0:
        raise ValueError('Expected canonical undirected CSR')
    base = 17 + (vertices+1)*8
    size = path.stat().st_size
    if size == base + arcs*8:
        wide = False
        edges = np.memmap(path, mode='r', dtype=np.dtype([('v', '<i4'), ('w', '<i4')]), offset=base, shape=(arcs,))
    elif size == base + arcs*16:
        wide = True
        edges = np.memmap(path, mode='r', dtype=np.dtype([('v', '<i8'), ('w', '<i4'), ('pad', '<i4')]),
                          offset=base, shape=(arcs,))
    else:
        raise ValueError(f'{path}: size {size} is neither the narrow nor the wide layout')
    version = 2 if wide or force_v2 else 1
    offsets = np.memmap(path, mode='r', dtype='<u8', offset=17, shape=(vertices+1,))
    out = path.with_suffix('.v2.gr' if force_v2 and not wide else '.gr')
    with out.open('wb') as f:
        f.write(struct.pack('<QQQQ', version, 4, vertices, arcs))
        for first in range(1, vertices+1, CHUNK):
            f.write(offsets[first:first+CHUNK].tobytes())
        for first in range(0, arcs, CHUNK):
            f.write(edges['v'][first:first+CHUNK].astype('<u8' if version == 2 else '<u4').tobytes())
        if version == 1 and arcs % 2:
            f.write(b'\x00'*4)
        for first in range(0, arcs, CHUNK):
            f.write(edges['w'][first:first+CHUNK].astype('<i4').tobytes())
    print(out, f'version={version}', flush=True)
