#!/usr/bin/env python3
"""Small undirected .wsg with many leaves, for the two-node --leaf-prune gate.

A random recursive tree (vertex i > 0 joins a random earlier vertex), plus
EXTRA x V random extra edges that avoid the last vertex, so vertex V - 1 is
always a leaf and can serve as a degree-1 source. Weights are uniform in
[1, 1000] with one edge in 16 of weight 1. Written as GAPBS undirected (header
byte 0 = 0, every edge in both endpoints' lists, num_edges = arcs).

  leafy_graph.py OUT.wsg [V] [EXTRA] [SEED]
"""
import sys
import numpy as np

out = sys.argv[1]
V = int(sys.argv[2]) if len(sys.argv) > 2 else 200000
extra = float(sys.argv[3]) if len(sys.argv) > 3 else 0.3
rng = np.random.default_rng(int(sys.argv[4]) if len(sys.argv) > 4 else 7)
child = np.arange(1, V)
parent = (rng.random(V - 1) * child).astype(np.int64)
k = int(extra * V)
a = rng.integers(0, V - 1, k)
b = rng.integers(0, V - 1, k)
keep = a != b
u = np.concatenate([child, a[keep]])
v = np.concatenate([parent, b[keep]])
w = rng.integers(1, 1001, len(u))
w[rng.random(len(u)) < 1 / 16] = 1
src = np.concatenate([u, v]); dst = np.concatenate([v, u]); wt = np.concatenate([w, w])
order = np.lexsort((dst, src))
src, dst, wt = src[order], dst[order], wt[order]
offsets = np.zeros(V + 1, dtype=np.int64)
np.cumsum(np.bincount(src, minlength=V), out=offsets[1:])
arcs = np.empty((len(src), 2), dtype=np.int32)
arcs[:, 0] = dst; arcs[:, 1] = wt
with open(out, 'wb') as f:
    f.write(bytes([0])); f.write(np.int64(len(src)).tobytes()); f.write(np.int64(V).tobytes())
    f.write(offsets.tobytes()); f.write(arcs.tobytes())
deg = np.diff(offsets)
print(f"{out}: V={V} arcs={len(src)} leaves={np.mean(deg == 1):.3f} deg(V-1)={deg[-1]}")
