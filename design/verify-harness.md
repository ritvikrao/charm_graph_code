# The `--verify` correctness harness

*Step 2 of [sc27-plan.md](sc27-plan.md). Written 2026-09-10.*

Before this, the code had no correctness check of any kind: `print_distances`
printed nothing (its body was commented out) and the only reduced result was a
single scalar, `get_max_cost`. Every performance number in the 2024 paper rests
on an unvalidated result, and the refactor the plan calls for — type erasure,
`Locator`, `CombiningHold` — cannot safely proceed without a way to prove the
answer did not move.

## What it does

`--verify` adds a second reduction after the run converges. Each chare folds its
slice of the distance vector into a `DistanceDigest`, and PE 0 recomputes the
same graph with serial Dijkstra and compares. On mismatch the program exits
nonzero, so a script can gate on it.

    ./sssp_smp 10000 160000 1 1 1 0.999 0.005 --verify +ppn 4

Flags are now parsed separately from the positional arguments, so `--verify` may
appear anywhere on the command line.

## Two properties that make it useful

**The digest is order-independent.** It is a pair of 64-bit sums over
`(vertex, distance)` — plus a reachable count and a distance sum — rather than a
sequential hash. Summation is commutative, so the result does not depend on the
order vertices are visited in or on which PE owns them.

**The graph is PE-count-independent.** `graph_gen.h` makes every edge and weight
a pure function of the global vertex id and the seed. Previously the mesh
generator seeded `mt19937(vertex + S)` and the random generator seeded
`mt19937(vertex)` — dropping the seed entirely, so `-S` had no effect on
generated graphs. Weights are now drawn from `hash(u, v, seed)` instead of from
the topology stream, which also makes weight assignment an independent knob, as
step 5's `WeightAssigner` will need.

Together these mean the same digest must appear at any PE count. Confirmed
identical at ppn 1, 2, 3, 4, 6, and 8.

## Why the golden file exists

`--verify` alone cannot catch a change to the graph generator, because the
parallel and serial paths share it — both would move together and still agree.
`scripts/golden_digests.txt` pins the graphs themselves, so a generator change
has to be re-recorded deliberately with `--update-golden`.

## Running the gate

    scripts/verify.sh                  # 10 configurations, mesh and random
    scripts/verify.sh --update-golden  # re-record after an intended change

`SSSP_PE_FLAG` selects the worker-thread flag (`+ppn` for Reconverse, `+p` for a
classic multicore build) and `SSSP_MAKE_ARGS` passes `CHARMC_SMP` / `HTRAM_DIR`
overrides through to make. `.github/workflows/verify.yml` runs the gate on every
push against a stock multicore Charm++ build; correctness is independent of the
Converse layer, and a stock build is fast and cacheable.

The gate was itself tested against a deliberately corrupted result (one vertex,
+7) and a perturbed golden file; both are caught.

## One defect pulled forward from step 3

`dest_table` was allocated `new int[V / M]` — a floor — while the loop
initialising it writes `ceil(V/M)` entries, overrunning the allocation by one
int whenever `V % M != 0`, which is every graph in `graphs/` and every
configuration in the gate.

This surfaced as an intermittent `SIGBUS` inside an unrelated `operator new` on
a worker thread — the system allocator tripping over heap metadata corrupted
much earlier. It reproduced roughly two runs in three at `V=50000, ppn=4` and
never at `ppn=1`, which is why it had gone unnoticed: the failure rate rises
with vertex count and thread count, and the single-threaded case looks clean.
It is fixed here rather than in step 3 because the gate cannot run without it.

Also fixed: in `generate_local_graph`, the early `continue` for vertices past a
PE's share skipped both `local_graph[i] = new_node` and
`largest_outedges[i] = 0`, so a reduction summed uninitialised memory. It is
unreachable in the current partitioning, but it sits directly on the path the
digest reads.

## Limits

- `--verify` requires a generated graph (mode 1 or 2). The file reader has no
  serial reference yet; it gets one in step 5, alongside the GAPBS `.sg`/`.wsg`
  reader.
- Serial Dijkstra regenerates adjacency on demand and is single-threaded, so the
  gate is practical to about 10^6 edges — which is what the plan specifies.
  Larger runs stay unverified until a distributed reference exists.
- The CI workflow has not yet run; it needs one push to shake out.
