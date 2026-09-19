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
import argparse
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


def tiled_order(order, tile, pes):
    """Deal compact tiles to P owners, then concatenate each owner's tiles.

    The permutation has no padding, including a partial last tile and P > V.
    It defines contiguous owner ranges for equal-vertex partitioning; ACIC's
    existing equal-edge reader may place boundaries slightly differently.
    """
    if tile <= 0 or pes <= 0:
        raise ValueError('tile and pes must be positive')
    owner = (np.arange(len(order), dtype=np.int64) // tile) % pes
    return order[np.argsort(owner, kind='stable')]


def edge_partition(offsets, pes):
    """The current MODE_GAPBS reader's exact boundaries (not V/P)."""
    n = len(offsets) - 1
    per_pe = max(1, (int(offsets[-1]) + pes - 1) // pes)
    boundaries = [0]
    for i in range(pes - 1):
        vertex = boundaries[-1]
        stop = min(n, max(vertex, n - (pes - i - 1)))
        target = int(offsets[vertex]) + per_pe
        boundaries.append(min(stop, int(np.searchsorted(offsets, target))))
    return np.asarray(boundaries + [n], dtype=np.int64)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root')
    parser.add_argument('graph')
    parser.add_argument('out')
    parser.add_argument('kind', choices=['mesh', 'coords', 'ordered'])
    parser.add_argument('arg', nargs='?')
    parser.add_argument('--tile', type=int)
    parser.add_argument('--pes', type=int)
    parser.add_argument('--reference-workers', type=int, default=2)
    args = parser.parse_args()
    if (args.tile is None) != (args.pes is None):
        parser.error('--tile and --pes must be supplied together')
    if args.tile is not None and (args.tile < 1 or args.pes < 1):
        parser.error('--tile and --pes must be positive')
    if args.reference_workers < 1:
        parser.error('--reference-workers must be positive')
    if args.kind != 'ordered' and args.arg is None:
        parser.error('mesh needs SIDE; coords needs the coordinate file')
    root, graph, out, kind, arg = args.root, args.graph, args.out, args.kind, args.arg
    if out == graph:
        parser.error('output must differ from input')
    g = Path(root) / 'graphs'
    with open(g / f'{graph}.wsg', 'rb') as f:
        directed, m, n = struct.unpack('=Bqq', f.read(17))
    off = np.asarray(np.memmap(g / f'{graph}.wsg', mode='r', dtype='<i8', offset=17, shape=(n + 1,)))
    edges = np.asarray(np.memmap(g / f'{graph}.wsg', mode='r', dtype='<i4', offset=17 + (n + 1) * 8, shape=(m, 2)))
    if kind == 'mesh':
        side = int(arg)
        if side <= 0 or side * side != n:
            raise ValueError('mesh SIDE must square to the vertex count')
        x, y = np.divmod(np.arange(n, dtype=np.int64), side)
    elif kind == 'coords':
        raw = gzip.open(arg, 'rb').read()
        a = np.fromstring(raw[raw.index(b'\nv ') + 1:].replace(b'v', b' '), dtype=np.int64, sep=' ').reshape(-1, 3)
        assert len(a) == n and (a[:, 0] == np.arange(1, n + 1)).all()
        x, y = a[:, 1] - a[:, 1].min(), a[:, 2] - a[:, 2].min()
    order = (np.arange(n, dtype=np.int64) if kind == 'ordered' else
             np.argsort(morton(x, y), kind='stable'))  # order[new] = old
    if args.tile is not None:
        order = tiled_order(order, args.tile, args.pes)
    rank = np.empty(n, dtype=np.int64)
    rank[order] = np.arange(n)                       # rank[old] = new
    src = rank[np.repeat(np.arange(n, dtype=np.int64), np.diff(off))]
    dst = rank[edges[:, 0].astype(np.int64)]
    w = edges[:, 1]
    perm = np.lexsort((dst, src))
    src, dst, w = src[perm], dst[perm], w[perm]
    new_off = np.zeros(n + 1, dtype=np.int64)
    np.cumsum(np.bincount(src, minlength=n), out=new_off[1:])
    if directed:
        raise ValueError('directed GAPBS files need an inverse CSR; unsupported')
    with open(g / f'{out}.wsg', 'wb') as f:
        f.write(struct.pack('=Bqq', directed, m, n))
        f.write(new_off.astype('<i8').tobytes())
        f.write(np.stack([dst.astype('<i4'), w.astype('<i4')], axis=1).tobytes())
    rank.astype('<i8').tofile(g / f'{out}.perm')
    (g / f'{out}.meta').write_text((g / f'{graph}.meta').read_text())
    vi.GRAPH = csr_matrix((w.astype(np.float64), dst, new_off), shape=(n, n))
    rows = [l.rstrip('\n').split('\t') for l in open(g / f'{graph}.reference.txt') if l[0].isdigit()]
    if not rows:
        raise ValueError('input reference contains no sources')
    with multiprocessing.get_context('fork').Pool(min(len(rows), args.reference_workers)) as pool:
        got = pool.map(vi.digest, [int(rank[int(r[0])]) for r in rows])
    lines = ['source\trole\th1\th2\treachable\tdistance_sum\tmax_distance\treachable_arcs']
    for r, d in zip(rows, got):
        assert d['distance_sum'] == int(r[5]) and d['max_distance'] == int(r[6]) and d['reachable'] == int(r[4]), (r, d)
        lines.append('\t'.join(map(str, [d['source'], r[1], d['h1'], d['h2'], d['reachable'],
                                         d['distance_sum'], d['max_distance'], r[7]])))
    (g / f'{out}.reference.txt').write_text('\n'.join(lines) + '\n')
    for P in ([args.pes] if args.pes else [112, 224, 896]):
        boundaries = edge_partition(new_off, P)
        cut = (np.searchsorted(boundaries[1:], src, side='right') !=
               np.searchsorted(boundaries[1:], dst, side='right')).mean() if m else 0.0
        print(f'{out} P={P} cut={cut:.4f}')
    import json
    (g / f'{out}.placement.json').write_text(json.dumps({
        'input': graph, 'order': kind, 'tile': args.tile, 'pes': args.pes,
        'partition': 'equal-edge (existing ACIC reader)',
    }, indent=2) + '\n')
    print(f'{out}: wrote {n} vertices, {m} arcs; distance sums match {graph}')


if __name__ == '__main__':
    main()
