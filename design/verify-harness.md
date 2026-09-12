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

## Graph generation is portable, and that took a second pass

The first CI run failed, and failed usefully. Mesh digests matched between the
macOS laptop and the Linux runner; random-graph digests did not.

The cause was `std::uniform_int_distribution`. The standard fixes the *engines*
but not the *distributions*, so libc++ and libstdc++ return different sequences
from the same engine and the same seed. The mesh generator draws no
distribution -- its weights come from `hash(u, v, seed)` -- which is exactly why
it agreed and the random generator did not.

This was never really a CI problem. It means the same command line produces a
different graph on Frontier, on Vista, and on a laptop, so no cross-machine
number is comparable and the artifact appendix cannot promise a reader our
inputs. `--verify` cannot see it: the parallel and serial paths share the
generator, so on any one machine both move together and still agree. Only the
golden file caught it.

Generation now uses no `<random>` at all. `VertexRng` is a counter-based stream
built from SplitMix64, and bounded draws use Lemire's multiply-shift, so
everything is fixed-width integer arithmetic the language pins down. The
per-PE partition sizes in `Main` were converted too -- they do not affect the
answer, since a vertex's adjacency depends only on its global id, but they do
change load balance, and performance runs should be structurally identical
across machines.

`scripts/check_generator_portability.sh` compiles `tools/graph_digest.cpp` with
every toolchain it can find and checks they agree. It needs no Charm++ build,
so it runs anywhere and is a separate, fast CI job. Verified across libc++ and
libstdc++ on macOS ARM64, and across three toolchains on Linux x86-64 in a
container -- all six configurations identical on both architectures.

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

## What `--verify` cannot see, and the gate that can

`scripts/verify.sh` runs everything in one process, and one whole class of defect
is invisible there. When htram sends a message to a PE in the same process, the
receiver is handed the same pointer and reads the entire allocation — the message
envelope's declared size is never consulted. Only a message that crosses an
address-space boundary is truncated to that size. So a send path that under-states
how many bytes its payload occupies produces correct results at any PE count on one
node, and an out-of-bounds read on two.

That is not hypothetical: step 4's varsize change moved the payload offset from 8
to 16, and the size formula that had been correct-by-accident at offset 8 would
have under-sized every odd-count send by 8 bytes. See
[varsize-messages.md](varsize-messages.md) §3 and §7.

`--verify` would not have caught it even on two nodes. The truncated field here is
the high half of a `cost`, and every distance in these runs fits in 32 bits, so the
lost bytes are zeros and the digest still matches. Two things close the gap:

- **htram asserts the invariant at the receiver.** Every landing point compares the
  envelope that arrived against the bytes the items need, and aborts if it is
  short. One comparison per message; compiled out with
  `-DHTRAM_NO_ENVELOPE_CHECK`.
- **`scripts/verify_2node.sh`** runs the `--verify` matrix across two nodes, one
  process each, sweeping `--bufsize` (which decides how many sends are partial
  flushes, and so how often a size formula is exercised at all) and
  `LCI_ATTR_PACKET_SIZE` (which decides eager versus rendezvous). It needs a
  Slurm allocation, so it is a pre-merge gate rather than a per-commit one.

## A third check, added in step 5

Checks 1 and 2 above share the graph generator: `--verify` compares the parallel
result against a serial solve of the *same* generated graph, so if the generator
changes, both move together and still agree — and the golden file only notices if
nobody re-records it. That is a real blind spot, and it was hit within the hour:
moving the RNG into `graphlib/rng.h` added a stream label mixed in as
`^ splitmix64(stream)`, which at the default `stream = 0` looks like a no-op and is
not, because `splitmix64(0)` is not zero. Every generated graph moved.

The gate now also runs **`tools/graph_digest`**, which builds without Charm++ and
shares no code with the solver beyond graphlib itself, and requires it to produce
the same digest. Eighteen configurations, covering uniform, mesh, RMAT, and graphs
round-tripped through a GAPBS `.wsg` file.

## Limits

- `--verify` still refuses mode 0, the CSV reader. That reader builds the graph
  inside `Main` rather than from a `GraphSpec`, so the reference solver has nothing
  to rebuild independently. Convert those files with `tools/graph_convert csv` and
  use mode 4, which *is* covered — a graph generated in memory, written to `.wsg`,
  and read back solves to the same digest as the in-memory run.
- Serial Dijkstra is single-threaded and builds the whole graph in one process, so
  the gate is practical to about 10^6 edges — which is what the plan specifies.
  Larger runs stay unverified until a distributed reference exists.
- A source vertex with no outgoing edges does not converge: the termination test
  needs `updates_created > 1000`, which such a run never reaches. Easy to hit on
  RMAT and on real graphs. `start_algo` warns; the predicate is step 7's.
- The whole CI pipeline was re-run locally in a Linux x86-64 container against
  the golden file recorded on macOS ARM64, and passes: generator portability,
  a stock multicore Charm++ build, and all ten gate configurations.
