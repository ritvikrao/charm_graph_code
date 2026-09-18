# IPDPS27 sprint: claims, evidence and experiments

*Started 2026-09-18. Working document for the
[IPDPS27 sprint](sc27-plan.md#ipdps27-submission-sprint-2026-09-18-to-10-08).
Abstract due October 1 AOE, full paper October 8 AOE, go/no-go September 26.
The paper outline is in [ipdps27-paper.md](ipdps27-paper.md).*

## 1. Claim and evidence table

Each row is a sentence the paper would like to print, what currently supports
it, and the experiment that decides it. "Supported" means clean cells above the
allocation floor. Cells from one allocation are marked as such.

| # | Claim (draft wording) | Status | Evidence now | Decided by |
|---|---|---|---|---|
| C1 | On sparse, high-diameter graphs, ACIC solves weighted SSSP faster than distributed Δ-stepping (RIKEN) and asynchronous bulk-communication SSSP (Gluon-Async) on equal CPU nodes | **supported, one allocation per node count** | 8g, 8 nodes: 19× (mesh24) and 53× (mesh26) ahead of RIKEN; 8.1×, 7.4× and 79× ahead of Gluon-Async on mesh24, mesh26, road-usa. 2 nodes: 37× and 9.8× on mesh24 | E3: RIKEN and Gluon re-taken on wider grids (RIKEN's search chose its grid's edge); a second allocation; road-usa-w4 so RIKEN has a real road cell |
| C2 | The gain comes from specific mechanisms added since the workshop paper, each with a causal time-to-solution effect | **open** | Step 7–8 A/Bs on the build of their day, one mechanism at a time, mostly at 2 nodes; never all on one binary | E1 (ablation on one binary, 2 and 8 nodes, two allocations) |
| C3 | The runtime feedback (adaptation) matters: it beats a strong fixed configuration | **open, and was negative on the old build** | 7.6f2: adaptive lost road-usa to tuned fixed admission by 7.6–10×; co-design not shown; 7.6k's buffer-size feedback never acted | E1's `global-fixed`, `tuned-fixed`, `buffer-no-feedback` and `no-coarsen` arms. If they tie with `current`, the title drops "adaptive" |
| C4 | ACIC scales from 2 to 8 nodes | **scale-free yes; high-diameter only with a locality-preserving order** | 8g: rmat25 1.4×, orkut 1.09×, rmat26 2.0×, rmat27 2.3× faster at 8 nodes; native mesh24 0.44 → 0.41 s, road-usa 1.36 → 1.33 s (flat). E2: re-work across PE boundaries is the cause; with Morton order road-usa-z goes 1.50 → 0.88 s from 2 to 8 nodes (one allocation) | E1 and E3 on the `-z` inputs (second allocation); only claimed where measured |
| C5 | ACIC is competitive on scale-free graphs | **not supported against RIKEN** | RIKEN ahead 1.9–3.7× at 8 nodes (8g), the lead growing 2 → 8 nodes on RMAT; ACIC ahead of Gluon-Async 2.3–17× | Reported as a loss, with 8a's work-growth attribution. Not chased (sprint rule) |
| C6 | Relative to a strong one-node code | **not supported** | 7.6n: one-node GAPBS faster than ACIC on every graph (road-usa 0.15 s vs ACIC 1.33 s at 8 nodes; mesh24 0.19 vs 0.41 s) | Reported prominently. E2 decides whether any part of it is recoverable; the paper states the COST-style ratio |
| C7 | All reported distances are correct | **supported for every timed run; independent check in progress** | Every 8g run matched its reference digest (table in §4). The references come from ACIC's repository C++ | E4 (independent validator: raw downloads or numpy generators, scipy Dijkstra) |

The provisional thesis is C1 with C2. C3 decides the title. C5 and C6 are
losses the paper must state and explain, not omit.

## 2. What is new since IA³@SC24 (overlap check)

**Caveat:** the workshop PDF is not on Anvil or Delta (`~/Downloads/acic_2024paper.pdf`
is referenced but absent). This table is reconstructed from the code at
charm_graph_code `50ec8f2` (2024-11-21, the last 2024 commit) and the plan's
account of the paper. **Check it against the PDF before the abstract**; the
CFP requires the submission to be substantially new and the predecessor to be
explained without breaking anonymity.

| Element | In the 2024 code? | Kind | Since then |
|---|---|---|---|
| Asynchronous SSSP over htram; histogram of live updates by distance; percentile thresholds for heap admission and sending | yes | mechanism (published) | Kept. Correctness of its control path repaired (step 3, 7.6a, 7.6g) |
| Buffer size | fixed at compile time (`BUFSIZE` 512 at `50ec8f2`; the runtime argument was ignored) | — | 7.6k: chosen by the controller from average degree (256 items per unit, 512–6144), with an acceptance-rate correction |
| Flush cadence | fixed (flush when the threshold stalls; `tflush` bug armed a 2048-iteration scan per edge) | — | Step 7.1: adaptive, gated by the controller's starvation signal |
| Bucket width | fixed by a formula | — | Step 7.3: coarsening by the controller; 7.6b clamp/two-tier rules; 7.6d width rule for meshes |
| Idle flush | stub (`IDLE_FLUSH` undefined) | — | Step 7.4: starvation-gated idle flush; 8e: minimum interval between idle flushes (30/100 µs by regime) |
| Lazy relaxation of heavy edges | no | — | 8d: light edges relaxed at once, heavier ranges deferred as tokens (G = 2), degree ≥ 8 |
| Delivery skipping | no | — | 8b: node deliveries skip PEs with no items, degree ≥ 8 |
| Sender-side dominance filter | no | — | 7.6j/k: hash table of last-sent distance per destination, on at buffer ≥ 2048 |
| Wire format | 16-byte update + 4-byte PE (24 B padded) | implementation | 7.6j: 8-byte compact item, destination recomputed on arrival |
| Destination lookup | per-update search | implementation | 7.6m: one-load table |
| Hold bitmap in htram | no | implementation | 8e: drains visit only buckets that hold items |
| Progress and termination | the default could deadlock; empty-window rescue missing | repair | 7.6a, 7.6g (`--pq-overflow-last`), stall rescue |
| Runtime | Charm++ (classic) | platform | Reconverse; `--enable-shmem` (7.6l) |
| Validation | none | methodology | Dijkstra digests, verify gate, matched-input harness, independent validator (E4) |
| Evaluation | two synthetic families, 16 nodes, no external baseline comparison of the current code | methodology | Real road/social inputs, RIKEN, Gluon-Async, GAPBS, per-system layout search |

The paper's new-mechanism claims can only be the "mechanism" rows marked as
new: adaptive flush cadence, coarsening, idle flush and its interval, the
buffer-size rule, lazy relaxation, delivery skipping and the send filter.
Repairs and implementation speedups are reported as such (C2's ablation
separates them: `ws24` keeps every repair; `ws24-wide` also restores the
old wire).

## 3. Pinned versions

The candidate is the 8g build. The ablation binary adds one switch
(`--skip-empty`) whose default reproduces 8g's behavior.

| Item | Pin |
|---|---|
| 8g ACIC binary | `campaign/bin/acic` = `acic_8g`, sha256 `cfdacb33f887…`; charm_graph_code `de0ed1c`, htram `7db9c0a` |
| Ablation binaries | `acic_ipdps` (compact wire), `acic_ipdps_wide` (`WIRE=wide`), `acic_ipdps_diag` (`-DACIC_DIAG -DVCOUNT`); charm_graph_code `717e571` + `--skip-empty`, htram `7db9c0a`; hashes in `campaign/bin/variants-manifest.txt` |
| Charm++ tree | `~/charm_reconverse/reconverse-linux-x86_64-mpicxx-v0916-shm` (charm_reconverse `f6c74074f`, Release, `--enable-shmem`) |
| Reconverse | fetched into that tree: `33b8c36be239` (main), clean |
| LCI | fetched into that tree: `ca88ce2c4b42`, clean |
| RIKEN Graph500-SSSP | `552f156297d9`, `PAGE_SIZE 4096` patch, `benchmarks/riken_driver.cpp`; binary `riken_sssp` sha256 `2e1ed22fa144…` |
| Galois/Gluon | `b67f94206a8c`, `benchmarks/gluon.patch` (timer and digest only); binary sha256 `193f8eb3e0a5…` |
| GAPBS | `2972aeb2703165`, `benchmarks/gap_driver.cpp` |
| Toolchain | GCC 11.2.0, OpenMPI 4.0.6 (UCX pml), srun `--mpi=pmi2`; `PMI_MAX_KVS_ENTRIES=100000`, `FI_CXI_RX_MATCH_MODE=hybrid` |
| Inputs | `campaign/graphs/*.wsg`, sha256 in `sha256.txt` / `sha256-large.txt`; road-usa-w4 added 09-18 |
| Sources | seed 20260913, two tuning and four held-out per graph (`*.reference.txt`) |

If a diagnosed change (E2) is adopted, the frozen build gets its own row and
E1's affected arms are re-run on it.

## 4. Validation

**Run level.** Every run's digest (h1, h2, reachable count, distance sum) must
equal the reference row. 8g audit, all four jobs: every ACIC, Gluon and RIKEN
test and comm-share run is `ok`; the only failures are 15 RIKEN tuning runs at
d1024 (crashes, 2 per graph, one on rmat26 at 8 nodes), excluded by its search.
No hangs, no wrong digests.

**Independent check (E4).** `benchmarks/validate_independent.py` rebuilds each
graph without any campaign C++: road-usa/road-ny from the raw DIMACS download,
orkut from the raw SNAP file with the weight hash re-implemented, and mesh,
RMAT and uniform graphs by re-implementing `graphlib`'s generator arithmetic
in numpy. It canonicalizes by its own code, solves with scipy's Dijkstra and
compares the full digest row and the arc count. Small graphs, all sources run:

| graph | result |
|---|---|
| mesh20, rmat20, rmat20-s2, uniform20, road-ny | PASS (every test source; arc counts equal) |
| road-usa, road-usa-w4, orkut, mesh24, mesh26, uniform25 | PASS, four test sources each (jobs 20826213, 20826232) |
| rmat25 | running (job 20826213) |
| `-z` relabelings | distance sums and maxima equal the native graph's for all six sources (reorder_graph.py) |

rmat26 and rmat27 exceed scipy's int32 index range; they share rmat25's
generator path, and every system agrees on their digests.

**Input facts** (from the raw inputs, before canonicalization):

| graph | raw arcs | self-loops | repeated arcs | arcs without reverse | pairs whose directions disagree in weight |
|---|---:|---:|---:|---:|---:|
| road-ny (DIMACS) | 733,846 | 0 | 3,746 | 0 | 0 |
| mesh20 (generator) | 4,190,208 | 0 | 0 | 0 | 2,093,054 of 2,095,104 |
| rmat20 (generator) | 16,776,045 | 0 (dropped) | 691,030 | 15,316,313 | 383,962 of 384,351 |
| uniform20 (generator) | 16,764,135 | 9 | 0 | 16,763,878 | 124 of 124 |
| road-usa (DIMACS) | 58,333,344 | 0 | 624,720 | 0 | 0 |
| orkut (SNAP, each edge listed once) | 117,185,083 | 0 | 0 | all | — |
| mesh24 (generator) | 67,092,480 | 0 | 0 | 0 | 33,512,461 of 33,546,240 |

So: every input is used as an **undirected** graph with the **minimum** weight
of any parallel or opposite arcs; the generators' weights are a hash of the
ordered pair, so the two directions of a generated edge almost always differ
and canonicalization keeps the smaller. The paper must say this in one
sentence, and must not call the RMAT graphs Graph500 graphs (different
generator noise, integer weights 1–1000, no 64-root validation).

**Timing boundaries.**

| System | Timed region | Excluded |
|---|---|---|
| ACIC | `Compute time`: from the source's first update to the controller's termination, including all control and termination rounds | file read, CSR construction, per-vertex state, digest |
| RIKEN | max over ranks of `run_sssp` after an MPI barrier | read, construction, (disabled) presolve, digest |
| Gluon | `Timer_0` inside the solver | graph load and partitioning, digest |
| GAPBS | `DeltaStep` (allocates its distance and frontier vectors inside) | read, digest |

All four are solve-only. Load/build time is recorded for ACIC and RIKEN and
must appear in one table.

## 5. Experiments

### E1: ablation on one binary (the go/no-go experiment)

`run.py --mode ablation` (added 09-18). Every arm is the 8g default with one
mechanism off, on `acic_ipdps`, at 8g's per-graph layout:

| arm | flags | tests |
|---|---|---|
| `current`, `control` | — | default; allocation floor |
| `ws24` | flush every 5 rounds, no coarsening, no idle flush, buffer 2048, no filter, no lazy relaxation, no delivery skipping; repairs kept | all post-2024 mechanisms together |
| `ws24-wide` | `ws24` on the 16-byte wire build | wire format's share |
| `fixed-cadence` | flush every round | adaptive cadence (7.1) |
| `no-idle-flush` | `--idle-flush off` | starvation-gated idle flush (7.4) |
| `no-idle-interval` | `--idle-flush-interval 0` | idle-flush interval (8e) |
| `no-coarsen` | `--bucket-policy fixed` | coarsening (7.3) |
| `buffer-2048` | `--bufsize 2048`, filter state unchanged | the buffer-size rule (7.6k) |
| `buffer-no-feedback` | the rule's initial size, fixed | the rule's runtime correction (adaptivity) |
| `no-lazy` (degree ≥ 8) | `--lazy-heavy off`, skipping kept | lazy relaxation (8d) |
| `no-skip-empty` (degree ≥ 8) | `--skip-empty off` | delivery skipping (8b) |
| `no-filter` (filter on) | `--send-filter off` | send filter (7.6j/k) |
| `global-fixed` | every runtime-adaptive choice fixed; one setting from {flush 1, 5} × {logv, weight width} × {1024, 2048} chosen on the development graphs mesh20, rmat20, uniform20 | "a good fixed default" |
| `tuned-fixed` | the same space per graph plus a tuned width and buffer sizes 512–6144, chosen on the graph's own tuning sources | "best fixed per graph" |

Graphs: the Morton-ordered high-diameter inputs (E2) mesh24-z, road-usa-z,
road-usa-w4-z and mesh26-z (8 nodes), and rmat25, orkut, uniform25. Jobs
20826260/61 (scale-free, 2 and 8 nodes) and 20826391/92 (high-diameter). Nodes 2 and 8; four held-out sources × 2 repetitions per
allocation; two allocations per node count (the second may drop arms that
were within the floor on every graph). Reading rules as in 7.6h: a cell counts
only if clean and above the allocation floor, or the same sign in both
allocations. Records carry updates noted, distance changes, rejected updates,
TRAM messages and bytes, reductions and coarsenings for work and traffic.
Peak memory is not recorded by the solver; one diag run per graph supplies it.

Go/no-go reading (September 26): C2 holds if at least one new-mechanism arm is
reproducibly slower than `current` on the high-diameter class (or `ws24` is,
with the per-mechanism arms explaining it). C3 holds only if `current` beats
`global-fixed` clearly and stays within ~20% of `tuned-fixed` per graph, and
`buffer-no-feedback`/`no-coarsen` lose; otherwise the title and abstract drop
"adaptive".

Cost estimate: about 950 launches per allocation at ~6 s each plus slow
`ws24` cells: ~2.2 h at 8 nodes (~2,300 SU) and ~2 h at 2 nodes (~500 SU);
~5,600 SU for two allocations each. One-node smoke first (job 20826230).

### E2: why high-diameter solves do not speed up from 2 to 8 nodes

8g: mesh24 0.44 → 0.41 s, road-usa 1.36 → 1.33 s, while scale-free graphs
speed up 1.1–2.3×. Hypothesis H-HD: on these graphs the solve is a sequence
of controller rounds whose count is set by the distance range and bucket
width, not by PE count, and each round costs a reduction plus delivery
latency that grows with PEs while the work per round shrinks. If so, the
per-round time is flat or rising from 2 to 8 nodes and the round count is the
same; the fix is fewer or cheaper rounds (wider admission per round on these
graphs, or overlapping rounds), not less work.

Measurement (jobs 20826233/34/37, 1/2/8 nodes): one `acic_ipdps_diag` run per
graph (rounds.csv: per-round time, window, occupancy) and two
`acic_8g_comm` runs (compute/send/other/idle shares) on mesh24, road-usa, and
mesh26 at 8 nodes. The alternative H-HD2 is critical-path hops across PE
boundaries (1-D block partition of a row-major mesh gives 960 strips at 8
nodes), which predicts rising per-round latency with node count but not a
fixed round count. At most one change comes out of this, under the sprint's
two-change limit, with a paired A/B against 8g in two allocations.

**E2 result (09-18): the cause is the partition, not the rounds.** H-HD
is refuted. Controller rounds *fall* with node count, and the work grows.

| | nodes | solve (s) | distance changes per vertex | rounds | Σ PE work (s) | compute share |
|---|---:|---:|---:|---:|---:|---:|
| mesh24 | 1 | 0.46–0.87 | 9.1 | 1802 | 22–28 | 0.29–0.42 |
| mesh24 | 2 | 0.57–0.62 | 15.8 | 858 | 43 | 0.31–0.33 |
| mesh24 | 8 | 0.41–0.44 | 56.7 | 746 | 145 | 0.36–0.40 |
| road-usa | 1 | 1.58–1.64 | 8.6 | 6282 | 34–38 | 0.19–0.20 |
| road-usa | 2 | 1.75–1.77 | 14.5 | 4444 | 61–63 | 0.15–0.16 |
| road-usa | 8 | 1.53–1.54 | 38.3 | 1803 | 135–140 | 0.10 |

(jobs 20826233/34/37; diag build for changes and rounds, `acic_8g_comm` for
work and shares.) Dijkstra needs one distance change per vertex. The
re-relaxation grows with PE count, about as fast as the PEs are added, so
per-PE work and the solve time stay flat. ACIC gives each PE a contiguous
range of vertex IDs, and the fraction of edges that cross PEs is:

| ordering | 112 PEs | 224 PEs | 896 PEs |
|---|---:|---:|---:|
| mesh24 row-major (native) | 1.4% | 2.7% | 10.9% |
| mesh24 Morton | 0.34% | 0.49% | 1.0% |
| road-usa DIMACS (native) | 62% | 62% | 63% |
| road-usa Morton on DIMACS coordinates | 0.20% | 0.29% | 0.63% |

road-usa's native order is effectively a random partition for ACIC. A
distance that arrives late across a PE boundary re-relaxes everything the
receiving PE derived from the worse one, and at 896 PEs a row-major strip
is 4.6 rows thick. This is H-HD2 in a stronger form: the cost is re-work,
not only latency.

**Change 1: locality-preserving vertex order (input, not solver).**
`benchmarks/reorder_graph.py` writes `GRAPH-z`: the same graph relabeled in
Morton order of grid position (meshes) or DIMACS coordinates (roads), with
reference rows for the same physical sources (distance sums checked equal).
One allocation per node count, four runs each, graphs in blocks (jobs
20826370/71):

| | nodes | native → Morton (median s) | changes per vertex | rounds |
|---|---:|---|---|---|
| road-usa | 2 | 1.68 → 1.50 (1.12×) | 14.6 → 8.6 | 3855 → 3528 |
| mesh24 | 2 | 0.51 → 0.55 (0.93×) | 16.3 → 4.1 | 729 → 1631 |
| road-usa | 8 | 1.54 → 0.88 (**1.75×**) | 40.6 → 14.7 | 2034 → 1562 |
| mesh26 | 8 | 2.42 → 1.28 (**1.9×**) | 37.6 → 7.6 | 1501 → 1348 |
| mesh24 | 8 | 0.405 → 0.37 (1.1×) | 56.3 → 5.5 | 655 → 1028 |

Re-work falls 3–10× everywhere. Time follows on the larger graphs at 8
nodes, and road-usa-z now speeds up 1.7× from 2 to 8 nodes where native
road-usa was flat. mesh24 turns round-bound (rounds double at 2 nodes), so
its gain is small or negative. Because it is an input relabeling, every
system gets the same `-z` file (E3), and the paper reports native and
Morton orders side by side. It uses coordinates, which roads and meshes
have and scale-free graphs do not; the claim is limited accordingly.
Second-allocation confirmation comes from E1 and E3, which run on the `-z`
inputs.

The rmat27 2-node regression (2.02 → 2.48 s, likely G = 2) is noted but not
pursued: it is a scale-free cell and the sprint does not chase RIKEN there.

### E3: baselines at fair settings

8g's searches hit their grid edges: RIKEN chose 16 ranks per node (the most
offered) on every graph and the smallest delta offered (d16) on every RMAT
graph and mesh26. A fair-layout claim needs an interior optimum. Re-take
RIKEN with `--riken-layouts 8,16,32,64` and `--delta-divisors
256,128,64,16,4,1`, and Gluon-Async with its current grid, at 2 and 8 nodes
in a second allocation from E1's. Jobs 20826372/73 (rmat25, orkut,
uniform25, rmat26, rmat27 at 2 and 8 nodes) and 20826393/94 (mesh24,
mesh24-z, road-usa-w4, road-usa-w4-z, road-usa-z, and mesh26-z at 8
nodes: native and Morton orders for every system). One-node GAPBS against
ACIC on every paper graph, native and Morton: job 20826395. ACIC's `current` runs in the same
allocation as the pairing arm. GAPBS on one node on every graph that fits.

### E4: independent validation

§4. Jobs 20826213 (paper graphs) and 20826232 (road-usa-w4).

## 6. Budget and calendar

Balance on 09-18: 66.1 K SU. Planned sprint spend ≤ 12 K SU: E1 ~5.6 K, E3
~3 K, E2 and diagnoses ~0.5 K, reserve for one diagnosed change ~2 K.

| Date | Work |
|---|---|
| 09-18/19 | E4 and E2 measurements; E1 smoke then first allocations; this document and the paper skeleton |
| 09-20/21 | E1 second allocations; E3; E2 reading and at most one change |
| 09-22–25 | Change A/B (if any); tables; freeze build and data |
| 09-26 | Go/no-go against §5's rules |
