#!/usr/bin/env python3
"""Relabel a canonical .wsg so that contiguous ID ranges are compact regions.

ACIC gives each PE a contiguous range of vertex IDs. road-usa's DIMACS order
puts 62% of its edges across PEs at any PE count, and a row-major mesh becomes
thin strips (11% cut at 896 PEs); a Morton (Z-order) relabeling by position
cuts both below 1.1% at 896 PEs (design/ipdps27-sprint.md, E2). The graph is
the same graph: only IDs change, so every system can be given the relabeled
file.

    reorder_graph.py CAMPAIGN GRAPH OUT mesh SIDE
    reorder_graph.py CAMPAIGN GRAPH OUT coords USA-road-d.USA.co.gz

writes graphs/OUT.wsg, OUT.meta, OUT.perm (int64 new ID of each old ID) and
OUT.reference.txt: the same physical sources as GRAPH's reference, mapped to
their new IDs, with digests from scipy's Dijkstra (validate_independent.py's
contract). Distance sums and maxima must equal GRAPH's; the script checks.
"""
import gzip
import multiprocessing
import struct
import sys
from pathlib import Path

import numpy as np
from scipy.sparse import csr_matrix

sys.path.insert(0, str(Path(__file__).parent))
import validate_independent as vi  # noqa: E402


def morton(x, y, bits=32):
    x, y = x.astype(np.uint64), y.astype(np.uint64)
    z = np.zeros_like(x)
    for b in range(bits):
        z |= ((x >> np.uint64(b)) & np.uint64(1)) << np.uint64(2 * b)
        z |= ((y >> np.uint64(b)) & np.uint64(1)) << np.uint64(2 * b + 1)
    return z


def main():
    root, graph, out, kind, arg = sys.argv[1:6]
    g = Path(root) / 'graphs'
    with open(g / f'{graph}.wsg', 'rb') as f:
        directed, m, n = struct.unpack('=Bqq', f.read(17))
    off = np.asarray(np.memmap(g / f'{graph}.wsg', mode='r', dtype='<i8', offset=17, shape=(n + 1,)))
    edges = np.asarray(np.memmap(g / f'{graph}.wsg', mode='r', dtype='<i4', offset=17 + (n + 1) * 8, shape=(m, 2)))
    if kind == 'mesh':
        side = int(arg)
        x, y = np.divmod(np.arange(n, dtype=np.int64), side)
    else:
        raw = gzip.open(arg, 'rb').read()
        a = np.fromstring(raw[raw.index(b'\nv ') + 1:].replace(b'v', b' '), dtype=np.int64, sep=' ').reshape(-1, 3)
        assert len(a) == n and (a[:, 0] == np.arange(1, n + 1)).all()
        x, y = a[:, 1] - a[:, 1].min(), a[:, 2] - a[:, 2].min()
    order = np.argsort(morton(x, y), kind='stable')  # order[new] = old
    rank = np.empty(n, dtype=np.int64)
    rank[order] = np.arange(n)                       # rank[old] = new
    src = rank[np.repeat(np.arange(n, dtype=np.int64), np.diff(off))]
    dst = rank[edges[:, 0].astype(np.int64)]
    w = edges[:, 1]
    perm = np.lexsort((dst, src))
    src, dst, w = src[perm], dst[perm], w[perm]
    new_off = np.zeros(n + 1, dtype=np.int64)
    np.cumsum(np.bincount(src, minlength=n), out=new_off[1:])
    with open(g / f'{out}.wsg', 'wb') as f:
        f.write(struct.pack('=Bqq', 0, m, n))
        f.write(new_off.astype('<i8').tobytes())
        f.write(np.stack([dst.astype('<i4'), w.astype('<i4')], axis=1).tobytes())
    rank.astype('<i8').tofile(g / f'{out}.perm')
    (g / f'{out}.meta').write_text((g / f'{graph}.meta').read_text())
    vi.GRAPH = csr_matrix((w.astype(np.float64), dst, new_off), shape=(n, n))
    rows = [l.rstrip('\n').split('\t') for l in open(g / f'{graph}.reference.txt') if l[0].isdigit()]
    with multiprocessing.get_context('fork').Pool(len(rows)) as pool:
        got = pool.map(vi.digest, [int(rank[int(r[0])]) for r in rows])
    lines = ['source\trole\th1\th2\treachable\tdistance_sum\tmax_distance\treachable_arcs']
    for r, d in zip(rows, got):
        assert d['distance_sum'] == int(r[5]) and d['max_distance'] == int(r[6]) and d['reachable'] == int(r[4]), (r, d)
        lines.append('\t'.join(map(str, [d['source'], r[1], d['h1'], d['h2'], d['reachable'],
                                         d['distance_sum'], d['max_distance'], r[7]])))
    (g / f'{out}.reference.txt').write_text('\n'.join(lines) + '\n')
    for P in [112, 224, 896]:
        cut = ((src * P) // n != (dst * P) // n).mean()
        print(f'{out} P={P} cut={cut:.4f}')
    print(f'{out}: wrote {n} vertices, {m} arcs; distance sums match {graph}')


if __name__ == '__main__':
    main()
