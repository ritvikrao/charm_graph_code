#!/usr/bin/env python3
"""Translate canonical .wsg CSR to Galois .gr v1 without changing edges."""
from pathlib import Path
import struct
import sys
import numpy as np

for argument in sys.argv[1:]:
    path = Path(argument)
    with path.open('rb') as f:
        directed, arcs, vertices = struct.unpack('=Bqq', f.read(17))
    if directed or arcs < 0 or vertices <= 0:
        raise ValueError('Expected canonical undirected CSR')
    offsets = np.memmap(path, mode='r', dtype='<u8', offset=17, shape=(vertices+1,))
    edges = np.memmap(path, mode='r', dtype='<i4', offset=17+(vertices+1)*8, shape=(arcs, 2))
    with path.with_suffix('.gr').open('wb') as f:
        f.write(struct.pack('<QQQQ', 1, 4, vertices, arcs))
        f.write(offsets[1:].tobytes())
        for column in [0, 1]:
            for first in range(0, arcs, 1048576):
                f.write(edges[first:first+1048576, column].tobytes())
            if column == 0 and arcs % 2:
                f.write(b'\x00'*4)
    print(path.with_suffix('.gr'), flush=True)
