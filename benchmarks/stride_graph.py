#!/usr/bin/env python3
"""A narrow .wsg with its vertex ids multiplied by STRIDE, as a wide .wsg.

Vertex v of IN becomes v * STRIDE of OUT; the ids in between are isolated.
OUT has n * STRIDE vertices, so a stride past 2^32 / n puts the graph's ids on
both sides of 2^32 while it stays as small as IN in edges. Distances,
reachable counts and distance sums are those of IN from source s * STRIDE;
only the id-keyed digest halves (h1, h2) change. The 64-bit tests use it on
mesh20 (1,048,576 vertices, stride 4200: 4.4e9 ids).

  stride_graph.py IN.wsg STRIDE OUT.wsg
"""
import struct
import sys
import numpy as np

source, stride, target = sys.argv[1], int(sys.argv[2]), sys.argv[3]
with open(source, 'rb') as f:
    directed, m, n = struct.unpack('=Bqq', f.read(17))
off = np.fromfile(source, dtype='<i8', count=n+1, offset=17)
edges = np.fromfile(source, dtype=np.dtype([('v', '<i4'), ('w', '<i4')]), count=m, offset=17+(n+1)*8)
N = n * stride
CHUNK = 1 << 26
with open(target, 'wb') as f:
    f.write(struct.pack('=Bqq', directed, m, N))
    # Offset of id j: the edges before real vertex ceil(j / stride).
    for first in range(0, N+1, CHUNK):
        j = np.arange(first, min(N+1, first+CHUNK), dtype=np.int64)
        f.write(off[(j + stride - 1) // stride].astype('<i8').tobytes())
    wide = np.zeros(m, dtype=np.dtype([('v', '<i8'), ('w', '<i4'), ('pad', '<i4')]))
    wide['v'] = edges['v'].astype(np.int64) * stride
    wide['w'] = edges['w']
    f.write(wide.tobytes())
print(f'{target}: vertices={N} arcs={m} stride={stride}')
