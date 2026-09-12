# Step 5 — retirement, flat CSR, and graphlib v1

Plan step 5: *"Retire all non-SMP and dead code. `graphlib` v1: in-memory
Kronecker/RMAT/uniform/mesh generators + GAPBS `.sg`/`.wsg` binary reader; flat CSR
replacing per-vertex `std::vector`."*
Gate: *"Verify identical; real weighted graphs load for the first time."*

Both clauses hold. The ten pre-existing configurations produce byte-identical digests, and
the gate has grown from 10 to 18 configurations covering RMAT and files. The headline is
the last one: a graph can now be generated in memory, written to a GAPBS `.wsg`, read back
by an entirely different code path, and solved in parallel to the **same digest as the
in-memory run** — `h1=15882876744078177223` either way, which is the number already in
`scripts/golden_digests.txt`.

---

## 1. What was retired

Twenty-nine files, and three live code paths that did nothing.

**The non-SMP line.** `sssp_nonsmp`, `weighted`, `tramNonSmp` (both repos), `ig_nonSmp`,
`build_nonsmp.sh`, and the `libtramnonsmp.a` rules. It had diverged from the SMP path, two
of its dependents no longer compiled against the current `weighted_node_struct.h`, and
nothing in the evaluation used it. Retiring it also let `weighted_node_struct.h` drop
`#include "NDMeshStreamer.h"` — Charm++'s own TRAM, pulled into every translation unit
that touches a graph type purely as a leftover of that build.

**Four CSV read-timing prototypes**: `graph_serial`, `graph_ckio`, `graph_parallel`,
`graph_parallel_ckio`, plus `rmat_preprocess.py`, which converted PaRMAT output into that
CSV format. graphlib replaces all of it with generators and one binary reader, so the
text-parsing surface goes with them. `htram_group_unused.*` was 39 KB of a stale copy of
the library.

**Four entry methods whose bodies were entirely comments** — `Main::quiescence_detected`,
`Main::check_buffer_done`, `SsspChares::check_buffer`, `SsspChares::keep_going` — and
their `.ci` declarations. Each one's only callers were inside another one's commented-out
body, which is why the set had to be removed together or not at all.

Three things that were live and did nothing:

- **The app-side `tram_hold`.** `HISTO_BUCKET_COUNT` vectors per PE, each reserving 4096
  `Update`s, handed to htram's `shareArrayOfBuckets()` — which read the bucket count and
  dropped the pointer. Every `push_back` into it was already commented out; the library
  keeps its own per-destination hold. `bfs_hold` was allocated and never touched at all.
  `shareArrayOfBuckets` is now `setHistoBucketCount(int)`, and it *rejects* a count that
  disagrees with what the constructor allocated: `tram_hold`'s rows are sized by
  `histo_bucket_count` at construction and `insertBucketsByDest` walks up to it, so a
  larger count arriving later would have run off the end of every row. Latent only
  because the application's 2048 happened to match the library's hardcoded 2048.
- **`local_updates` and `process_local_updates`**, an abandoned local-delivery shortcut.
  Nothing pushed into the vector, so htram called back once per released batch to iterate
  something permanently empty. htram's `tram_done` is now optional — it was being invoked
  unconditionally at three sites even though the two-argument `set_func_ptr_retarr` leaves
  it null, so the first client to use that overload would have segfaulted.
- **`get_dest_proc_fast` on the `LOCAL_TO_TRAM` path** in `generate_updates`: computed once
  per outgoing edge into a variable that branch cannot reach.

`charmc`'s location moved to a `?=` default plus an untracked `config.mk`, so a machine no
longer needs an uncommittable diff to a tracked file — which is what the repository had.

---

## 2. Flat CSR

The topology was an array of `Node`, each holding its own `std::vector<Edge>`. Per vertex
of degree *d* that is 24 bytes of vector header, 8 for the distance, 4 for a
`home_process` field that was always `thisIndex`, 4 of padding, an allocator header, and
16*d* for the edges — with the edges scattered across the heap in generation order and
each vector's capacity rounded up by doubling, so up to half the edge bytes were slack.
`graphlib/csr.h` holds one offset array and one contiguous edge array: 8 bytes per vertex
and 16*d* contiguous bytes, walked as a single sequential read.

Measured on one Delta CPU node, `+ppn 8`, uniform random graphs:

| | peak RSS before | after | build time before | after |
|---|---|---|---|---|
| V=4M, E=64M | 1496 MB | 1388 MB | 1.03–1.32 s | 0.83–0.93 s |
| V=8M, E=128M | 2917 MB | 2574 MB | — | — |

Solve time is within run-to-run noise at these sizes either way (1.37–1.79 s versus
1.31–1.61 s over three runs each); the win here is footprint and construction, and the
locality argument needs a bigger frontier than these graphs produce to show up in wall
clock.

**Peak RSS badly understates it, and that is worth knowing before anyone quotes a memory
number from `/usr/bin/time`.** This runtime reserves a fixed 554 MB whatever the graph and
whatever the PE count: `567296 kB` appears identically at V=10 000 and at V=1M, at
`+ppn 1` and at `+ppn 8`. Net of that floor the topology is 11% smaller at 4M vertices and
14% at 8M. A run now reports `Graph bytes` from the inside, where the number is real:
17.0 bytes per edge all in, including offsets and the distance vector.

**The distance vector deliberately stays outside the CSR.** It is written on every accepted
update while the topology is read-only for the whole run, so interleaving them would dirty
a cache line of edges on every relaxation.

One mistake worth recording, because the 554 MB floor is exactly what hid it. The first
version of the builder filled scratch vectors and copied them into exactly-sized arrays.
That holds both copies at once, which made *peak* memory worse than the layout it
replaced — and the measurement showed `567296 kB` before and after, to the kilobyte,
because the floor swamped both. The build vectors are now the storage: reserved once from
the caller's estimate, moved into place, and copied only if the estimate was off by more
than an eighth.

---

## 3. graphlib v1

```
graphlib/  types.h        payload types; the one place that knows if Charm++ is present
           rng.h          portable determinism: no <random>, no libm, no floating point
           weights.h      weights as a function of (u, v), orthogonal to topology
           generators.h   uniform, 2-D mesh, RMAT/Kronecker
           gapbs.h        GAPBS .sg / .wsg reader and writer
           csr.h          flat CSR for one PE's slice
           edge_source.h  GraphSpec, and how a source reaches the PE that owns a row
           reference.h    serial Dijkstra and the order-independent distance digest
```

Header-only, and it builds with no runtime at all under `GRAPH_GEN_STANDALONE` — which is
what lets `tools/graph_digest` and `tools/graph_convert` run in CI and on a reviewer's
laptop.

### The distinction that organises it

A source is characterised by how a row reaches the PE that owns it, and there turn out to
be three answers, not two:

- **Vertex-indexed** (uniform, mesh). The adjacency of *v* is a function of *v* and the
  seed alone, so the owner generates its rows directly. No communication during
  construction, and the serial reference can regenerate any vertex on demand.
- **Edge-indexed** (RMAT). Edge *i* is a function of *i* and the seed, but its source
  vertex is an *output* of the draw. Every PE generates a contiguous slice of the edge
  index space and routes what it produces to the owners. This is inherent, not an
  implementation shortcut: an RMAT vertex's degree is the outcome of every edge draw in
  the graph, so no local rule can produce one vertex's adjacency.
- **Row-addressable** (GAPBS `.sg`/`.wsg`). The file is already CSR ordered by source
  vertex, so a PE seeks to its own rows and reads only those — no exchange and no parsing.

### RMAT

Graph500 quadrant probabilities (0.57 / 0.19 / 0.19 / 0.05), written as integers per
ten-thousand so the draw involves no floating point. Two deliberate departures from the
Graph500 reference:

- **No per-level noise.** Graph500 perturbs *(a, b, c)* at each level of the descent.
  Leaving it out costs a little of the degree distribution's tail and buys an edge that is
  a pure function of its own index — which is what makes any PE able to generate exactly
  its own slice with no communication and no shared state.
- **Self-loops dropped, duplicate edges kept.** A self-loop can never improve a distance;
  a duplicate is a legitimate parallel edge the solver handles. Dropping duplicates would
  need a global sort. At scale 14 with 16 edges per vertex, 348 of 262 144 draws are
  self-loops (0.13%).

Vertex labels are permuted, and this is not cosmetic. The descent builds an id one bit per
level, so without relabelling vertex 0 is the heaviest hub *by construction* and a
contiguous partitioning hands every hub to the first PE — a load-balance measurement would
be measuring the generator. `permute_id` composes three maps that are each individually
invertible mod 2^bits (add a constant, xor with a right shift of itself, multiply by an odd
number), so the result is a permutation rather than a hash that happens to spread out.

RMAT requires a power-of-two vertex count and says so, rather than silently using part of
the space: the generator descends one bit of the vertex id per level, so the vertex space
is 2^scale by construction.

The edge exchange uses **quiescence detection** for completion rather than counting
messages. Nothing knows in advance how many edges will arrive where, and an all-to-all of
per-destination counts would cost N² messages before the first edge moved.

### Weights

`WeightAssigner` makes the weight a pure function of *(u, v)* and the seed, not of the
per-vertex generation stream. That separation is the point: *"RMAT is hard because of its
topology"* and *"RMAT is hard because of its weight distribution"* are different claims,
the bucket width is tuned against weights in [1, 1000], and a reviewer will ask which one
is doing the work.

`UNIFORM` (max 1000, the 2024 configuration, and what every golden digest depends on) and
`UNIT` are implemented. `LOGNORMAL` and `DEGREE_CORRELATED` are named in the plan's
evaluation section and are **not** implemented, for reasons that are worth stating rather
than leaving as a gap: a lognormal drawn with `exp()` and `log()` would be at the mercy of
the host's libm and would break the cross-machine reproducibility everything else here
guarantees, so it needs an integer quantile table; and a degree-correlated weight needs
the endpoint's degree, which an edge-indexed generator does not know locally. Adding
either is a change to `weights.h` alone.

### The GAPBS format

Checked against GAPBS `src/writer.h` `WriteSerializedGraph` and `src/reader.h`
`ReadSerializedGraph`, not guessed. A raw dump of the CSR with no padding between fields:

```
offset 0    bool   directed            (1 byte)
offset 1    int64  num_edges           (directed edge count)
offset 9    int64  num_nodes
offset 17   int64  offsets[num_nodes+1]   -- in elements, not bytes
...         DestID neighbours[num_edges]
if directed: the same two arrays again, for the in-edges
```

`DestID` is `int32` for `.sg` and `{int32 v; int32 w}` for `.wsg`; GAPBS fixes both node
ids and weights at `int32` and refuses anything else. Header fields are not naturally
aligned — `num_edges` starts at byte 1 — so reads go through `memcpy`. Byte order is the
writer's; GAPBS makes the same assumption, and the header check rejects a foreign-endian
file rather than producing nonsense. The size check is the important one: it turns a wrong
format guess or a truncated file into an immediate, specific failure instead of a graph
full of garbage vertex ids.

Because the file is CSR ordered by source vertex and the partition is a contiguous vertex
range, **the partition is chosen on equal edge counts rather than equal vertex counts**.
The offsets array is the entire cost of knowing the degree distribution, and it has to be
read anyway. This is the one place that information is free, and on a real graph equal
vertex ranges are a bad split.

An unweighted `.sg` gets weights from the same `WeightAssigner` the generators use, which
is how an unweighted real-world topology — most of them — becomes an SSSP input at all.

`tools/graph_convert` writes `.wsg` from any generator and migrates the legacy
`graphs/*.csv` files. It always writes `directed`, which means building the transpose:
writing them as undirected would be a claim about the graph (that every edge appears in
both endpoints' lists) that the generators do not make. Because the file is a genuine
GAPBS file, GAPBS's own SSSP can be run on the identical input — which is what makes the
single-node reference comparison the plan asks for an actual comparison rather than an
approximate one.

---

## 4. The gate

From 10 configurations to 18, and from two checks to three. The third is new and earned
its place immediately.

1. `--verify` against in-process serial Dijkstra.
2. The digest against `scripts/golden_digests.txt`.
3. **`tools/graph_digest`, which shares no code with the solver beyond graphlib, must
   agree with both.**

Checks 1 and 2 cannot catch a change to the generator on their own — the parallel and
serial paths share it, so both move together and still agree, and check 2 only notices if
someone re-records the golden file. While moving the RNG into `graphlib/rng.h` this note's
author added a stream label to `IndexRng` and mixed it in as `^ splitmix64(stream)`. At the
default `stream = 0` that looks like a no-op and is not: `splitmix64(0)` is not zero, so
**every generated graph moved**. The golden file caught it; the stream is now folded into
the seed, where stream 0 reproduces the original formula exactly.

`check_generator_portability.sh` gained RMAT configurations and now also checks that the
`.wsg` writer produces byte-identical files across toolchains. The artifact appendix
claims a reader can regenerate our inputs; a `.wsg` that differed between compilers would
make that false.

One gate configuration had to be changed after it exposed something real. At scale 16,
vertex 0 has no outgoing edges — common on RMAT and on real graphs, where a large fraction
of vertices have out-degree zero. The convergence test in `reduce_histogram` requires
`updates_created > 1000` before it will believe a run has finished, so a source with no
out-edges produces a run that sits until `--timeout` instead of finishing instantly with a
one-vertex answer. `start_algo` now says so explicitly. Making the predicate itself handle
it is a change to the convergence logic and belongs with the tail work in step 7.

---

## 5. What is deliberately not done

- **Mode 0, the CSV reader in `Main`, still exists.** It reads the whole edge list serially
  on PE 0. It is the only way to load `graphs/*.csv`, and `tools/graph_convert csv` now
  migrates those to `.wsg`; it should go once they are migrated. `--verify` refuses it,
  because it builds the graph inside `Main` rather than from a `GraphSpec`, so the
  reference solver has nothing to rebuild independently.
- **`bucket_multiplier` for RMAT uses the `log(V)` rule**, the same as uniform. That is
  hypothesis H1 in step 6 — the bucket width having no resolution on a small-diameter
  graph — and changing it before it is measured would destroy the measurement.
- **The `thisIndex` / `CkMyPe()` conflation in `initialize_data`** is untouched; it is
  step 9's.
- **Reading the offsets array for the partition happens on PE 0**, so it costs
  8·(V+1) bytes there — 1.6 GB at 200M vertices. Fine at the sizes this step targets, and
  the fix (a distributed scan) is small when it is needed.
