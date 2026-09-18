#!/usr/bin/env python3
"""Check the campaign's reference digests without any of its C++ code.

Every timed run is accepted when its digest equals the one in
graphs/GRAPH.reference.txt. That file comes from reference_gap.cpp, which
reads the .wsg that prepare_graph.cpp wrote. Both stages are C++ that ACIC's
own repository supplies, so a conversion or generator bug would pass every
system's check at once. This script rebuilds each graph from its origin --
the raw DIMACS or SNAP download, or a numpy re-implementation of the
generator's published arithmetic (graphlib/rng.h, weights.h, generators.h) --
canonicalizes it by its own code, solves with scipy's Dijkstra, and compares
the published digest contract (benchmarks/common.h) row by row.

    validate_independent.py GRAPH KIND ARG... --campaign ROOT [--sources test|all|N]

    road-usa dimacs deps/USA-road-d.USA.gr.gz
    road-usa-w4 dimacs deps/USA-road-d.USA.gr.gz 4
    orkut    snap   deps/com-orkut.ungraph.txt.gz 1
    mesh24   mesh   16777216 1
    rmat25   rmat   33554432 1
    uniform25 uniform 33554432 1

It also reports what the raw input contained before canonicalization: arcs,
self-loops, repeated arcs, arcs with no reverse and pairs whose two
directions disagree in weight. scipy's csgraph holds int32 indices, so graphs
above 2^31 stored arcs (rmat26, rmat27) are out of reach here.
"""
import argparse
import gzip
import multiprocessing
import os
import sys
import time

import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import dijkstra

U = np.uint64
GOLDEN = U(0x9E3779B97F4A7C15)
MASK32 = U(0xFFFFFFFF)
np.seterr(over='ignore')


def splitmix(x):
    x = x + GOLDEN
    x = (x ^ (x >> U(30))) * U(0xBF58476D1CE4E5B9)
    x = (x ^ (x >> U(27))) * U(0x94D049BB133111EB)
    return x ^ (x >> U(31))


def mul_hi(a, n):
    """High 64 bits of a * n for n < 2^32 (Lemire's bounded draw)."""
    n = U(n)
    return ((a >> U(32)) * n + (((a & MASK32) * n) >> U(32))) >> U(32)


def stream_start(index, seed, stream=0):
    s = splitmix(np.array([(seed & 0xFFFFFFFF) + 0x5DEECE66D + stream], dtype=U))[0]
    return splitmix(index.astype(U) ^ s)


def draw(state0, k):
    """The k-th next() (1-based) of an IndexRng whose state started at state0."""
    return splitmix(state0 + U(k) * GOLDEN)


def weight(u, v, seed):
    s = splitmix(np.array([seed], dtype=U))[0]
    h = splitmix(splitmix(u.astype(U) ^ s) ^ (v.astype(U) * GOLDEN))
    return (h % U(1000)).astype(np.int64) + 1


def permute(x, bits, key):
    mask = U((1 << bits) - 1)
    shift = U(bits // 2 if bits > 1 else 1)
    for r in range(4):
        x = (x + splitmix(np.array([key + r], dtype=U))[0]) & mask
        x = (x ^ (x >> shift)) & mask
        x = (x * (splitmix(np.array([key + 64 + r], dtype=U))[0] | U(1))) & mask
    return x


# ------------------------------------------------------------------ sources --

def dimacs(path, divide=1):
    with gzip.open(path, 'rb') as f:
        raw = f.read()
    start = raw.index(b'\na ') + 1
    header = raw[:start].decode()
    n = next(int(l.split()[2]) for l in header.splitlines() if l.startswith('p '))
    body = raw[start:].replace(b'a', b' ')
    del raw
    a = np.fromstring(body, dtype=np.int64, sep=' ').reshape(-1, 3)
    # road-usa-w4 divides each native weight by four, rounding up.
    return n, a[:, 0] - 1, a[:, 1] - 1, (a[:, 2] + divide - 1) // divide


def snap(path, seed):
    with gzip.open(path, 'rb') as f:
        lines = f.read().split(b'\n')
    body = b'\n'.join(l for l in lines if l and not l.startswith(b'#'))
    del lines
    a = np.fromstring(body, dtype=np.int64, sep=' ').reshape(-1, 2)
    u, v = a[:, 0], a[:, 1]
    lo, hi = np.minimum(u, v), np.maximum(u, v)
    w = (splitmix(splitmix(lo.astype(U) ^ U(seed)) ^ hi.astype(U)) % U(1000)).astype(np.int64) + 1
    ids = np.unique(np.concatenate([u, v]))
    return len(ids), np.searchsorted(ids, u), np.searchsorted(ids, v), w


def mesh(n, seed):
    side = int(np.floor(np.sqrt(float(n))))
    us, vs = [], []
    x, y = np.divmod(np.arange(side * side, dtype=np.int64), side)
    vertex = x * side + y
    for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
        nx, ny = x + dx, y + dy
        ok = (nx >= 0) & (ny >= 0) & (nx < side) & (ny < side)
        us.append(vertex[ok])
        vs.append(nx[ok] * side + ny[ok])
    u, v = np.concatenate(us), np.concatenate(vs)
    return n, u, v, weight(u, v, seed)


def rmat(n, seed, chunk=1 << 25):
    scale = int(n).bit_length() - 1
    assert 1 << scale == n
    key = int(splitmix(np.array([(seed & 0xFFFFFFFF) ^ 0x5045524D], dtype=U))[0])
    us, vs = [], []
    for first in range(0, 16 * n, chunk):
        index = np.arange(first, min(16 * n, first + chunk), dtype=np.int64)
        state = stream_start(index, seed, 0x524D4154)
        u = np.zeros(len(index), dtype=U)
        v = np.zeros(len(index), dtype=U)
        for level in range(scale):
            d = mul_hi(draw(state, level + 1), 10000)
            bit = U(1 << level)
            v |= np.where(((d >= 5700) & (d < 7600)) | (d >= 9500), bit, U(0))
            u |= np.where(d >= 7600, bit, U(0))
        u, v = permute(u, scale, key).astype(np.int64), permute(v, scale, key).astype(np.int64)
        keep = u != v
        us.append(u[keep])
        vs.append(v[keep])
    u, v = np.concatenate(us), np.concatenate(vs)
    return n, u, v, weight(u, v, seed)


def uniform(n, seed, average=16, chunk=1 << 22):
    top = 2 * average
    us, vs, fixups = [], [], 0
    for first in range(0, n, chunk):
        vertex = np.arange(first, min(n, first + chunk), dtype=np.int64)
        state = stream_start(vertex, seed)
        degree = np.minimum(mul_hi(draw(state, 1), top + 1).astype(np.int64), n)
        cand = np.stack([mul_hi(draw(state, k + 2), n).astype(np.int64) for k in range(top)], axis=1)
        take = np.arange(top)[None, :] < degree[:, None]
        # A row that drew a repeat redraws it; those rows are rare, so they are
        # replayed one draw at a time below.
        s = np.sort(np.where(take, cand, -1 - np.arange(top)[None, :]), axis=1)
        repeat = (s[:, 1:] == s[:, :-1]).any(axis=1)
        good = ~repeat
        us.append(np.repeat(vertex[good], degree[good]))
        vs.append(cand[good][take[good]])
        for i in np.nonzero(repeat)[0]:
            fixups += 1
            out, k = [], 2
            while len(out) < degree[i]:
                c = int(mul_hi(draw(state[i:i + 1], k), n)[0])
                k += 1
                if c not in out:
                    out.append(c)
            us.append(np.full(len(out), vertex[i], dtype=np.int64))
            vs.append(np.array(out, dtype=np.int64))
    u, v = np.concatenate(us), np.concatenate(vs)
    print(f'uniform rows redrawn: {fixups}', flush=True)
    return n, u, v, weight(u, v, seed)


# ---------------------------------------------------------- canonicalization --

def raw_stats(n, u, v, w):
    """What the directed input held before canonicalization."""
    loops = int((u == v).sum())
    key = u * n + v
    order = np.argsort(key, kind='stable')
    k = key[order]
    repeated = int((k[1:] == k[:-1]).sum())
    uniq = np.unique(key)
    rev = np.unique(v * n + u)
    no_reverse = int(len(uniq) - np.isin(uniq, rev, assume_unique=True).sum())
    # Pairs whose two directions disagree in weight: min over each direction.
    first = np.ones(len(k), dtype=bool)
    first[1:] = k[1:] != k[:-1]
    wmin = np.minimum.reduceat(w[order], np.nonzero(first)[0])
    ku = k[first]
    lo_first = (ku // n) < (ku % n)
    a_keys, a_w = ku[lo_first], wmin[lo_first]
    b_keys = (ku % n) * n + (ku // n)
    b_keys, b_w = b_keys[~lo_first], wmin[~lo_first]
    common, ia, ib = np.intersect1d(a_keys, b_keys, assume_unique=True, return_indices=True)
    disagree = int((a_w[ia] != b_w[ib]).sum())
    return dict(arcs=len(u), self_loops=loops, repeated_arcs=repeated,
                arcs_without_reverse=no_reverse, pairs=len(common),
                pairs_weight_disagree=disagree)


def canonical(n, u, v, w):
    keep = u != v
    lo, hi, w = np.minimum(u, v)[keep], np.maximum(u, v)[keep], w[keep]
    order = np.lexsort((w, lo * n + hi))
    key = (lo * n + hi)[order]
    first = np.ones(len(key), dtype=bool)
    first[1:] = key[1:] != key[:-1]
    lo, hi, w = lo[order][first], hi[order][first], w[order][first]
    rows = np.concatenate([lo, hi])
    cols = np.concatenate([hi, lo])
    data = np.concatenate([w, w]).astype(np.float64)
    del lo, hi, w, key, order
    return csr_matrix((data, (rows, cols)), shape=(n, n))


# ------------------------------------------------------------------- digest --

GRAPH = None


def digest(source):
    t = time.time()
    d = dijkstra(GRAPH, directed=True, indices=source)
    finite = np.isfinite(d)
    di = np.where(finite, d, 0).astype(np.int64)
    if (np.abs(d[finite] - di[finite]) > 0).any():
        raise ValueError('non-integer distance')
    du = np.where(finite, di.astype(U), U(0xFFFFFFFFFFFFFFFF))
    vu = np.arange(len(d), dtype=U)
    h1 = int(splitmix(vu * GOLDEN ^ splitmix(du)).sum(dtype=U))
    h2 = int(splitmix(du * U(0xC2B2AE3D27D4EB4F) ^ splitmix(vu + U(1))).sum(dtype=U))
    return dict(source=source, h1=h1, h2=h2, reachable=int(finite.sum()),
                distance_sum=int(di[finite].sum()), max_distance=int(di[finite].max()),
                seconds=time.time() - t)


def main():
    global GRAPH
    p = argparse.ArgumentParser()
    p.add_argument('graph')
    p.add_argument('kind', choices=['dimacs', 'snap', 'mesh', 'rmat', 'uniform'])
    p.add_argument('args', nargs='+')
    p.add_argument('--campaign', required=True)
    p.add_argument('--sources', default='test', help='test, all, or the first N test sources')
    p.add_argument('--procs', type=int, default=int(os.environ.get('SLURM_CPUS_PER_TASK', 4)))
    p.add_argument('--no-raw-stats', action='store_true')
    a = p.parse_args()
    t = time.time()
    if a.kind == 'dimacs':
        n, u, v, w = dimacs(a.args[0], int(a.args[1]) if len(a.args) > 1 else 1)
    elif a.kind == 'snap':
        n, u, v, w = snap(a.args[0], int(a.args[1]))
    else:
        n, u, v, w = dict(mesh=mesh, rmat=rmat, uniform=uniform)[a.kind](int(a.args[0]), int(a.args[1]))
    print(f'{a.graph}: built {len(u)} input arcs on {n} vertices in {time.time()-t:.0f} s', flush=True)
    if not a.no_raw_stats:
        print(f'{a.graph} RAW', ' '.join(f'{k}={x}' for k, x in raw_stats(n, u, v, w).items()), flush=True)
    GRAPH = canonical(n, u, v, w)
    del u, v, w
    meta = dict(x.split('=') for x in open(f'{a.campaign}/graphs/{a.graph}.meta').read().split())
    arcs_ok = int(meta['arcs']) == GRAPH.nnz and int(meta['vertices']) == n
    print(f'{a.graph} CANON vertices={n} arcs={GRAPH.nnz} max_weight={int(GRAPH.data.max())} '
          f'meta_vertices={meta["vertices"]} meta_arcs={meta["arcs"]} match={arcs_ok}', flush=True)
    rows = [l.split('\t') for l in open(f'{a.campaign}/graphs/{a.graph}.reference.txt')
            if l[0].isdigit()]
    if a.sources == 'all':
        pick = rows
    else:
        test = [r for r in rows if r[1] == 'test']
        pick = test if a.sources == 'test' else test[:int(a.sources)]
    ctx = multiprocessing.get_context('fork')
    with ctx.Pool(min(a.procs, len(pick))) as pool:
        results = pool.map(digest, [int(r[0]) for r in pick])
    bad = 0
    for r, got in zip(pick, results):
        want = dict(h1=int(r[2]), h2=int(r[3]), reachable=int(r[4]),
                    distance_sum=int(r[5]), max_distance=int(r[6]))
        ok = all(got[k] == want[k] for k in want)
        bad += not ok
        print(f'{a.graph} SOURCE {got["source"]} {r[1]} {"MATCH" if ok else "MISMATCH"} '
              + ' '.join(f'{k}={got[k]}' for k in want)
              + ('' if ok else ' expected ' + ' '.join(f'{k}={want[k]}' for k in want))
              + f' dijkstra_s={got["seconds"]:.1f}', flush=True)
    print(f'{a.graph} RESULT {"PASS" if bad == 0 and arcs_ok else "FAIL"} '
          f'{len(pick) - bad}/{len(pick)} sources, {time.time()-t:.0f} s', flush=True)
    return 0 if bad == 0 and arcs_ok else 1


if __name__ == '__main__':
    sys.exit(main())
