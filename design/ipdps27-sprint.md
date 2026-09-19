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
| C3 | Adaptive message-flow control (§1b: decisions from the graph as read and from live message flow, built on Charm++ abstractions) is what makes ACIC fast | **C3a, C3b supported; C3c not yet** | C3a, flow control driven by runtime events is worth 12–22× on high-diameter graphs at 8 nodes (`ws24`), and the idle flush alone 2–3.6×. C3b, decisions taken when the graph is read match per-graph hand tuning within ±18% and beat the best single fixed setting by up to 7.6×. C3c, live parameter feedback (buffer correction, coarsening, the starvation gate) ties with the same mechanism at a fixed parameter | E1, both allocations. C3c: [L3](ipdps27-onenode-gap.md#l3-live-control-of-how-far-ahead-a-pe-may-run) is designed to make it hold |
| C4 | ACIC scales from 2 to 8 nodes | **scale-free yes; high-diameter only with a locality-preserving order** | 8g: rmat25 1.4×, orkut 1.09×, rmat26 2.0×, rmat27 2.3× faster at 8 nodes; native mesh24 0.44 → 0.41 s, road-usa 1.36 → 1.33 s (flat). E2: re-work across PE boundaries is the cause; with Morton order road-usa-z goes 1.50 → 0.88 s from 2 to 8 nodes (one allocation) | E1 and E3 on the `-z` inputs (second allocation); only claimed where measured |
| C5 | ACIC is competitive on scale-free graphs | **not supported against RIKEN** | RIKEN ahead 1.9–3.7× at 8 nodes (8g), the lead growing 2 → 8 nodes on RMAT; ACIC ahead of Gluon-Async 2.3–17× | Reported as a loss, with 8a's work-growth attribution. Not chased (sprint rule) |
| C6 | 8-node ACIC is no slower than any one-node run (GAPBS or ACIC) on every paper graph | **not supported; now a requirement for submission** | C6 table: one-node GAPBS beats 8-node ACIC on road-usa-z (5.8–7.5×), mesh24-z (2.1×) and mesh26-z (1.7×); 8-node ACIC beats one-node GAPBS 1.4–5× on scale-free graphs | [ipdps27-onenode-gap.md](ipdps27-onenode-gap.md): D0, then L1–L3; the go/no-go on 09-26 is that this passes |
| C7 | All reported distances are correct | **supported for every timed run; independent check in progress** | Every 8g run matched its reference digest (table in §4). The references come from ACIC's repository C++ | E4 (independent validator: raw downloads or numpy generators, scipy Dijkstra) |

The thesis is C1 with C2 and C3, under C6. C6 is a condition for
submitting, not a loss the paper states (author, 09-18). C5 stays a stated
loss.

## 1b. What "adaptive" means in this paper

**The author's definition (09-18).** Adaptivity is control of the message
flow in real time. That covers:

- live changes to the aggregation library (htram);
- choices of ACIC parameters from what is known about the graph when it is
  read;
- choices from how messages are flowing during the solve.

The paper's argument is that these controls are easy to express with
Charm++'s abstractions and hard in MPI or a bulk-synchronous runtime, and
that each one measurably changes time to solution.

**The narrower test used in E1 so far (C3c).** For each runtime-adjusted
parameter, E1 asked whether changing it during the solve beats a well-chosen
constant: `buffer-no-feedback`, `no-coarsen`, `idle-ungated`, `global-fixed`
and `tuned-fixed`. That test is a reviewer's question ("is it just a good
constant?"), and it is only one part of the definition. Under the author's
definition the evidence splits into three parts:

| Part | What it covers | Mechanisms | Evidence (E1, two allocations) |
|---|---|---|---|
| C3a: flow control driven by runtime events | Messages leave, are held, or are admitted according to runtime state, not a schedule | histogram admission (2024); the idle flush (fires when the scheduler finds a PE idle) and its interval; the starvation-gated flush cadence; lazy relaxation, which defers heavy edges as tokens released by the window | **Supported.** Together (`ws24`) 12–22× on high-diameter graphs and 2.9–4.3× on scale-free graphs at 8 nodes; `no-idle-flush` /2.1–/3.6; `no-lazy` /2.2–/3.3 |
| C3b: decisions from the graph as read | Parameters chosen once from V, E, degree and degree skew | buffer size from average degree (256 items per unit, 512–6144); the lazy-relaxation and delivery-skipping regime (degree ≥ 8, degree CV ≥ 1); the idle-flush interval regime; the send filter; L1's tile size (planned) | **Supported.** Within ±18% of per-graph tuned fixed settings with no tuning (2 and 8 nodes); up to 7.6× ahead of the best single fixed setting; `buffer-2048` /1.3–/2.6 on roads |
| C3c: live feedback on a parameter | A parameter changed during the solve from measured flow | buffer-size correction by acceptance rate; coarsening; the starvation gate on the idle flush; L3's run-ahead slack (planned) | **Not yet.** Ties with fixed values almost everywhere. Exceptions: uniform25 at 8 nodes (`buffer-no-feedback` /1.97, `no-coarsen` /1.69, second allocation only); `local-delivery` /1.31 on orkut. Coarsening costs road-usa-z 1.14–1.20× |

**Charm++ abstraction behind each mechanism.** This is the paper's argument
that these mechanisms are "easy in Charm++, hard in MPI". It is a
programmability argument, so it goes in Design (§3 of the paper) as a table,
with an honest statement of what an MPI version would need:

| Mechanism | Charm++ / Reconverse feature it uses | What an MPI code would need |
|---|---|---|
| Histogram controller overlapped with the solve (2024) | asynchronous reductions and broadcasts delivered as messages | `MPI_Iallreduce` plus a hand-written progress loop that polls it between relaxations |
| Idle flush | scheduler idle callback (`CcdPROCESSOR_BEGIN_IDLE`) | a user-level scheduler that detects idleness itself |
| Live changes to htram (flush cadence, buffer size, delivery skipping, holds released by threshold) | htram as a Charm++ group whose methods the controller's broadcast invokes on every PE | a custom aggregation layer whose parameters every rank changes at a matching point |
| Lazy relaxation tokens | message-driven entry methods that carry work across rounds | explicit deferral queues, re-checked by the progress loop |
| L1 tiles (planned) | over-decomposition: many pieces per PE, placed by the runtime's map | a custom partition and index translation |
| L2 shared state within a process (planned) | SMP mode: PEs of a process share memory and the node queue | MPI+threads with explicit locking, or MPI-3 shared-memory windows |
| L3 live run-ahead slack (planned) | the same reduction-broadcast cycle, with PEs picking up the new value at their next delivery or idle pass | the same progress loop, with a way to act on the value mid-epoch |

**Title.** The 2024 paper's title is already "An Adaptive Asynchronous
Approach for the Single-Source Shortest Paths Problem", and its abstract
names "an adaptive aggregation library". The new title must differ. The new
paper's adaptivity differs from 2024's in what it controls. In 2024, the
only live control was the two admission percentiles, and the buffer size
was chosen by hand per node count. Now the aggregation layer, flush timing
and relaxation order are also controlled. Working title: *Adaptive Message
Flow for Asynchronous SSSP on High-Diameter Graphs*.

## 2. What is new since IA³@SC24 (overlap check)

**Checked against the PDF** ([acic_2024paper.pdf](acic_2024paper.pdf), IA³@SC24:
Rao, Chandrasekar, Kale, "An Adaptive Asynchronous Approach for the
Single-Source Shortest Paths Problem") on 09-18, and against the code at
`50ec8f2` (2024-11-21). The CFP requires the submission to be substantially
new, and the predecessor to be explained in the third person.

**What the 2024 paper contains:**

- **Algorithm:** histogram reductions to PE 0 with bucket width
  d / log|V|; two percentile thresholds, `t_tram` (send or hold) and `t_pq`
  (heap or hold); all thresholds opened when fewer than 100·|PE| updates are
  live; a heap holding only improving updates; tram holds drained in bucket
  order; a flush after every broadcast; termination by created and
  processed counters in the same reduction.
- **Tuning:** WP aggregation. The buffer size was one of 512, 1024 or 2048,
  **chosen by hand per node count** (Fig. 6). There was a one-node sweep of
  the percentiles, and a reduction-overhead microbenchmark.
- **Evaluation:** uniform and RMAT graphs at scale 26, 1–16 nodes on Delta
  and Frontier, against RIKEN. ACIC was 1.3–1.8× faster on uniform graphs
  and 2.8–3.3× slower on RMAT.
- **Future work, which this paper now delivers in part:** high-diameter
  (road) graphs; work sharing within a process; over-decomposition with
  migration; a threshold function of the whole histogram; 2D or 1.5D
  partitioning.

**Discrepancies the paper must handle:**

- **Flush cadence.** The 2024 paper says it flushed after every broadcast.
  The 2024 code flushed on a random one in five (`if(rand()%5==0)
  tram->tflush()`). `ws24` follows the code (every 5 rounds). The
  `fixed-cadence` arm (every round) ties with `current` everywhere, so the
  difference does not move any `ws24` cell by more than the floor. The
  paper describes `ws24` as "the 2024 design as released".
- **Uniform graphs.** 2024 had ACIC ahead of RIKEN by 1.3–1.8×. Now RIKEN
  is ahead by 1.1× on uniform25 at 8 nodes. The difference: the old
  comparison used RIKEN at fixed settings and a different graph. The paper
  must say that with RIKEN searched over layout and delta, the uniform
  result reverses, and it must not cite the 2024 number as current.
- **"Adaptive" is already in the 2024 title and abstract.** See §1b for
  what is new.

| Element | In the 2024 code? | Kind | Since then |
|---|---|---|---|
| Asynchronous SSSP over htram; histogram of live updates by distance; percentile thresholds for heap admission and sending | yes | mechanism (published) | Kept. Correctness of its control path repaired (step 3, 7.6a, 7.6g) |
| Buffer size | paper: one of 512/1024/2048, picked by hand per node count; code: fixed at compile time (`BUFSIZE` 512; the runtime argument was ignored) | — | 7.6k: chosen by the controller from average degree (256 items per unit, 512–6144), with an acceptance-rate correction |
| Flush cadence | paper: every broadcast; code: a random one broadcast in five (`tflush` bug armed a 2048-iteration scan per edge) | — | Step 7.1: adaptive, gated by the controller's starvation signal |
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
| Evaluation | uniform and RMAT at scale 26, 1–16 nodes, RIKEN at fixed settings | methodology | Real road/social inputs, RIKEN, Gluon-Async, GAPBS, per-system layout search |
| Vertex placement | 1-D contiguous ranges; paper names 2D/1.5D and over-decomposition as future work | — | Change 1 (Morton order); L1 tiles (planned) |
| Work sharing within a process | paper names it as future work | — | L2 (planned) |

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

## 5b. Results so far

### E1, first allocation per node count

Paired medians against `current` (8 pairs: four held-out sources × 2), `/x`
meaning the arm is x times slower. **Bold**: clears the allocation floor.
`·`: inside it. Scale-free on `acic_ipdps` (8g behavior), jobs 20826260 (2
nodes, floor 1.07×) and 20826261 (8 nodes, floor 1.29×, set by uniform25;
rmat25 1.06×, orkut 1.08×). High-diameter on the Morton inputs, job
20826391 (2 nodes, floor 1.06×).

| arm | rmat25 2n | rmat25 8n | orkut 2n | orkut 8n | uniform25 2n | mesh24-z 2n | road-usa-z 2n | road-usa-w4-z 2n |
|---|---|---|---|---|---|---|---|---|
| `ws24` (all post-2024 mechanisms off) | **/2.11** | **/3.03** | **/1.35** | **/3.23** | **1.70** | **/4.72** | **/5.35** | **/5.43** |
| `ws24-wide` (also the old wire) | **/2.09** | **/11.4** | **/2.74** | **/8.75** | **1.12** | **/4.24** | **/5.06** | **/4.92** |
| `no-lazy` | **/1.22** | **/3.26** | **/1.33** | **/2.24** | **1.55** | — | — | — |
| `no-filter` | **/1.15** | /1.07 ~ | 1.04 ~ | 1.00 · | **1.12** | — | — | — |
| `fixed-cadence` | **/1.11** | /1.07 ~ | 1.02 ~ | 1.02 · | 1.02 ~ | **/1.07** | 1.01 ~ | 1.03 · |
| `no-idle-flush` | 1.05 · | 1.04 · | 1.00 · | /1.09 ~ | 1.00 · | **/1.32** | **/1.09** | **/1.07** |
| `no-idle-interval` | 1.00 · | 1.03 · | 1.05 ~ | 1.07 · | 1.01 ~ | **/1.12** | **/1.08** | **/1.10** |
| `no-coarsen` | /1.07 · | 1.00 · | 1.05 ~ | 1.05 · | 1.01 ~ | 1.04 ~ | **1.14** | 1.04 · |
| `buffer-2048` | 1.00 · | 1.01 · | /1.06 ~ | 1.07 · | 1.03 ~ | **/1.10** | **/1.88** | **/1.87** |
| `buffer-no-feedback` | /1.03 · | 1.01 · | /1.04 ~ | 1.08 · | 1.02 ~ | **/1.12** | 1.05 ~ | 1.05 · |
| `no-skip-empty` | 1.01 · | 1.03 · | 1.01 · | 1.04 · | 1.01 ~ | — | — | — |
| `global-fixed` | **/1.22** | /1.07 ~ | /1.06 ~ | /1.17 ~ | **/1.07** | **/1.19** | **/1.73** | **/1.75** |
| `tuned-fixed` | **/1.09** | /1.03 · | **1.13** | **/1.33** | **1.48** | /1.03 ~ | **/1.10** | **/1.07** |

Reading, one allocation:

- **C2 holds on both classes.** Together the post-2024 mechanisms are worth
  2–3× on scale-free graphs and ~5× on high-diameter ones. Lazy relaxation
  carries the scale-free gain (2.2–3.3× at 8 nodes). On high-diameter graphs
  the buffer-size rule (1.9× on roads), idle flush and its interval, and
  adaptive cadence each clear the floor. The compact wire is a further
  ~3× on scale-free graphs at 8 nodes and nothing on high-diameter ones;
  it is an implementation gain and is reported as one.
- **C3 holds on the high-diameter class at 2 nodes:** the default beats the
  good fixed setting by 1.19–1.75× and the per-graph tuned fixed setting by
  1.03–1.10×. On scale-free graphs it is mixed: tuned-fixed wins orkut at 2
  nodes (1.13×) and loses it at 8 (1.33×). The adaptivity claim, if made,
  is for the high-diameter class.
- **uniform25 runs better with lazy relaxation off**, which led to change 2.
- Nothing hung, crashed or answered wrongly in 1,008 ablation runs.

**High-diameter at 8 nodes** (job 20826392, Morton inputs, floor 1.03×):

| arm | mesh24-z | mesh26-z | road-usa-z | road-usa-w4-z |
|---|---|---|---|---|
| `ws24` | **/17.6** | **/12.6** | **/21.0** | **/18.5** |
| `ws24-wide` | **/18.6** | **/12.4** | **/18.9** | **/19.3** |
| `no-idle-flush` | **/3.60** | **/2.05** | **/2.59** | **/3.06** |
| `buffer-2048` | **/1.69** | **/1.16** | **/2.55** | **/2.57** |
| `no-idle-interval` | **/1.08** | **/1.06** | **/1.15** | **/1.05** |
| `fixed-cadence` | /1.02 ~ | **1.07** | 1.00 · | /1.01 ~ |
| `no-coarsen` | /1.01 ~ | /1.01 · | 1.01 · | 1.01 ~ |
| `buffer-no-feedback` | **/1.04** | 1.02 · | /1.01 · | /1.02 ~ |
| `global-fixed` | **/4.11** | **/2.40** | **/6.35** | **/6.05** |
| `tuned-fixed` | **/2.70** | **/1.09** | **/3.35** | **/3.03** |

At 8 nodes the post-2024 mechanisms are worth 12–21× on high-diameter
graphs, and the default beats per-graph tuned fixed settings by 1.09–3.35×.
**Caveat on C3:** every fixed candidate so far had the idle flush off, and
the idle flush alone is worth 2–3.6× here. So "adaptive beats tuned fixed"
may only mean "idle flush beats no idle flush". The runtime feedback that
is left -- buffer-size correction, coarsening, adaptive cadence -- is
within 1.07× of off at 8 nodes. The test that decides C3 is running (jobs
20827705/06): fixed candidates with an ungated idle flush, and one-axis arms
that remove only the controller's gate (`idle-ungated`, and
`local-delivery`, 7.6f2's co-design arm).

**C3 test, 2 nodes** (job 20827705, frozen build `acic_ipdps2`, floor
1.02×). Fixed search space now includes an ungated idle flush (`-idle`
arms); `idle-ungated` and `local-delivery` remove only the controller's gate.

| arm | mesh24-z | road-usa-z | road-usa-w4-z | orkut | rmat25 |
|---|---|---|---|---|---|
| `tuned-fixed-idle` (chosen per graph) | **1.07** | 1.02 · | **/1.03** | 1.02 ~ | **/1.03** |
| `global-fixed-idle` (chose idle off) | **/1.12** | **/1.68** | **/1.73** | **/1.05** | **/1.95** |
| `idle-ungated` | **/1.14** | **/1.06** | **/1.05** | **1.07** | 1.00 · |
| `local-delivery` | /1.01 · | 1.01 · | **1.02** | 1.01 ~ | 1.02 ~ |

Tuned choices: every graph picked idle flush on, with buffers of 512 (roads,
mesh) or 6144 (scale-free). So at 2 nodes: **the defaults match per-graph
tuned fixed settings (within ±7%) without any tuning, and beat the best
single fixed setting by 1.05–1.95×.** The feedback itself -- the
controller's gate on delivery, buffer correction, coarsening -- adds
nothing measurable; local-delivery ties, as in 7.6f2. The defensible C3 is
"no per-graph tuning needed", from regime rules plus an idle flush, not
"feedback beats tuning" and not co-design.

**C3 test, 8 nodes** (job 20827706, floor 1.10×): `tuned-fixed-idle` 1.18×
faster on mesh24-z and 1.06× on mesh26-z, 1.11× slower on road-usa-z and
rmat25, within the floor elsewhere; `global-fixed-idle` /2.86–/4.99 on the
high-diameter graphs, ±1.05 on scale-free ones; `idle-ungated` ties on the
high-diameter graphs (/1.11 on orkut); `local-delivery` ties except /1.31
on orkut. Same reading at 8 nodes: defaults within ±18% of per-graph
tuning, up to 5× better than one fixed setting; the gate matters only on
orkut.

### Change 2: lazy relaxation only on skewed degree distributions

On uniform25 lazy relaxation leaves traffic unchanged (1.07 × 10^9 updates,
8.6 GB with or without it) and adds rounds; on rmat25 and orkut it cuts
updates 1.4–4.8×. The auto gate now also requires a degree coefficient of
variation of at least 1 (`--lazy-skew`, commit 2943ab5). Paired A/B of the
old and new binaries in two allocations per node count (jobs
20826591/93/95/97): uniform25 at 2 nodes 0.89 → 0.57 s (**1.5×**, 16/16
pairs); at 8 nodes within noise. **Open:** rmat25 at 2 nodes was slower
with the new binary in 8/8 pairs (1.09× and 1.17×; 5% more updates),
although the gate gives the same answer there (CV 12). Job 20827103
separates the binary from the gate (new binary with `--lazy-skew 0`).

### E3, baselines at fair settings

8 nodes (jobs 20826373 and 20827060, the second adding d1/d2), median solve
seconds over four held-out sources. RIKEN's choices are now interior: 32
ranks per node (of 8–64) and d4–d8 on RMAT (of d1–d1024).

| graph | ACIC | RIKEN (choice) | Gluon-Async | RIKEN lead | ACIC over Gluon |
|---|---:|---:|---:|---:|---:|
| rmat25 | 0.237 / 0.252 | 0.074 / 0.069 (r32 d4) | 1.76 | 3.2× / 3.65× | 7.2× |
| orkut | 0.098 | 0.031 (r16 d64) | 1.99 | 3.2× | 20× |
| rmat26 | 0.394 / 0.374 | 0.121 / 0.124 (r32 d4–d8) | 1.94 | 3.0× | 5.4× |
| rmat27 | 0.742 / 0.796 | 0.223 / 0.232 (r32 d4) | 2.55 | 3.35× / 3.4× | 3.5× |
| uniform25 | 0.355 | 0.317 (r32 d256) | 3.23 | 1.1× | 9.1× |

2 nodes (job 20826372): RIKEN leads 3.2× (rmat25), 2.2× (orkut), 3.2×
(rmat26), 3.5× (rmat27), 1.6× (uniform25, before change 2). With RIKEN
tuned fairly, its scale-free lead is ~3× at both node counts: it no longer
grows from 2 to 8 nodes on RMAT, and it does not shrink. 8g's RIKEN numbers
(r16, d16 at the grid edge) understated it by up to 2.5× (orkut 0.079 →
0.031 s).

**High-diameter** (jobs 20826393 at 2 nodes, 20826394 at 8; median solve
seconds over four held-out sources; RIKEN on road-usa-w4 has one valid
source of four, see §5b's go/no-go table):

| graph | nodes | ACIC | RIKEN (choice) | Gluon-Async | ACIC over RIKEN | ACIC over Gluon |
|---|---:|---:|---:|---:|---:|---:|
| mesh24 | 2 | 0.557 | 12.3 (r64 d64) | 3.66 | 22× | 6.6× |
| mesh24-z | 2 | 0.588 | 10.6 (r64 d1024) | 6.96 | 18× | 12× |
| road-usa-w4 | 2 | 1.36 | 118 (r32 d512) | 34.8 | ~86× | 26× |
| road-usa-w4-z | 2 | 1.11 | 115 (r64 d512) | 18.2 | ~100× | 16× |
| road-usa-z | 2 | 0.914 | — | 19.4 | — | 21× |
| mesh24 | 8 | 0.420 | 6.99 (r32 d64) | 3.29 | 17× | 7.8× |
| mesh24-z | 8 | 0.380 | 6.03 (r32 d1024) | 5.32 | 16× | 14× |
| mesh26-z | 8 | 1.29 | 64.6 (r64 d64) | 22.6 | 50× | 17.5× |
| road-usa-w4 | 8 | 1.33 | 49.9 (r64 d512) | 94.4 | ~38× | 71× |
| road-usa-w4-z | 8 | 0.663 | 48.4 (r64 d512) | 9.36 | ~73× | 14× |
| road-usa-z | 8 | 0.689 | — | 9.46 | — | 14× |

- **C1 survives the widened baselines**: 16–50× over RIKEN on meshes,
  7.8–17.5× over Gluon-Async; on the Morton-ordered roads 14× over Gluon.
- **Morton order helps Gluon more than ACIC on roads** (94 → 9.4 s at 8
  nodes). Every system gets the `-z` file, so the fair road ratio is ~14×,
  not the 71–79× of native order.
- **C4 on high-diameter, second allocation:** ACIC on `-z` inputs gets
  faster from 2 to 8 nodes: road-usa-z 1.33×, road-usa-w4-z 1.68×, mesh24-z
  1.55×; road-usa-w4 native → Morton at 8 nodes 1.33 → 0.66 s (change 1
  replicated).
- RIKEN picked 64 ranks per node (the most the sizing rule allows) on four
  cells and d1024 (the largest offered) on mesh24-z. At 16–100× this cannot
  change a sign; it is stated as a limit of its search.

### C6: one node

Job 20826395, each system at its own best layout and delta on one node:

| graph | ACIC 1 node | GAPBS 1 node | GAPBS lead | best ACIC at 8 nodes |
|---|---:|---:|---:|---:|
| mesh24 / mesh24-z | 0.548 / 0.917 | 0.193 / 0.174 | 2.8× / 5.3× | ~0.37 (-z) |
| mesh26 / mesh26-z | 5.50 / 5.19 | 0.842 / 0.760 | 6.5× / 6.8× | 1.28 (-z) |
| road-usa / road-usa-z | 1.51 / 1.66 | 0.152 / 0.118 | 9.9× / 14× | 0.88 (-z) |
| rmat25 | 1.07 | 0.850 | 1.3× | 0.24 |
| orkut | 0.288 | 0.145 | 2.0× | 0.10 |
| uniform25 | 1.80 | 1.14 | 1.6× | 0.36 |
| rmat26 | 2.26 | 1.68 | 1.3× | 0.37 |
| rmat27 | 4.63 | 3.86 | 1.2× | 0.74 |

**This is the largest threat to the submission.** On the high-diameter
graphs that carry C1, one-node GAPBS beats 8-node ACIC: 7.5× on road-usa
(0.118 s against 0.88 s), 1.7× on mesh26, 2× on mesh24. On scale-free
graphs 8-node ACIC beats one-node GAPBS by 3.5–5×, but RIKEN beats both. At
one node ACIC changes each road-usa distance ~9 times where Dijkstra
changes it once.

**Superseded on 09-18 by the author:** a distributed-only claim is not
acceptable. The gap must close. Morton order makes ACIC *slower* at one
node (mesh24 0.55 → 0.92 s), which points at the wavefront sitting on a few
PEs as well as at re-work. The plan is in
[ipdps27-onenode-gap.md](ipdps27-onenode-gap.md), and it is the sprint's
main work until 09-26.

### E1, second allocation (2 nodes, frozen build)

Job 20827652, `acic_ipdps2`, floor 1.09×, 672 runs, no failures. Replicates
the first allocation's signs: `ws24` /2.29 (rmat25), /1.40 (orkut), /3.97
(mesh24-z), /5.03 (road-usa-z), /5.19 (road-usa-w4-z); `no-lazy` /1.56
(rmat25), /1.29 (orkut); `buffer-2048` /1.63–/1.80 on roads; `no-idle-flush`
/1.13–/1.27 on high-diameter graphs; `global-fixed` /1.33–/2.87 on
high-diameter graphs and /1.49 on rmat25. `prev-binary` (the first
allocations' build): /1.54 on uniform25 (change 2, third allocation), and
±7–16% with no consistent sign elsewhere -- the rebuild noise. Coarsening
costs road-usa-z in both allocations (`no-coarsen` 1.14×, 1.20× faster).

### E1, second allocation (8 nodes, frozen build)

Job 20827653, `acic_ipdps2`, floor 1.09×, 776 runs, no failures, 2 h 02 m
(~2,000 SU; launch overhead of 6–8 s per run dominates). Replicates the
first 8-node allocation: `ws24` /11.6–/21.9 on high-diameter graphs, /2.88
(rmat25), /4.30 (orkut), /1.53 (uniform25); `no-lazy` /3.00 (rmat25),
/2.72 (orkut); `no-idle-flush` /2.14–/3.31 and `buffer-2048` /1.26–/2.61 on
high-diameter graphs; `global-fixed` /2.63–/7.64 high-diameter, /1.82–/1.86
scale-free; `tuned-fixed` (idle off) /1.25–/2.77 high-diameter, /1.44
orkut. New: on uniform25 (lazy now off) the buffer-size feedback and
coarsening matter (`buffer-no-feedback` /1.97, `no-coarsen` /1.69), and
`no-idle-flush` is 1.26× faster; `prev-binary` (lazy on) is 1.10× faster
there, so change 2 is a 2-node gain (1.5×) and roughly neutral-to-slightly
negative at 8 nodes on uniform25.

### Interim go/no-go reading (09-18 evening)

| Gate condition (sc27-plan.md) | Reading |
|---|---|
| A new post-workshop mechanism with a causal, reproducible time-to-solution gain | **Met.** Lazy relaxation (scale-free, 1.3–3.3×), the degree-based buffer size (roads, 1.6–2.6×), the idle flush and its interval (high-diameter, 1.1–3.6×); together 2–3× (scale-free) and 4–21× (high-diameter); two allocations at 2 nodes, one at 8 so far |
| High-diameter wins survive independent validation and fair baseline layouts | **Met.** Validation (E4); widened RIKEN and Gluon searches, same `-z` inputs for every system: 16–50× over RIKEN, 7.8–17.5× over Gluon at 8 nodes (E3 table). RIKEN takes 100–170 s on road-usa-w4 at 2 nodes against ACIC's ~1.5 s, **and returns wrong distances from 3 of the 4 held-out sources** (reaching 14, 188 and 288 vertices) at every layout, delta and node count, in both vertex orders; it is right from the tuning sources and the fourth test source. Our driver passes other graphs; the cause is not diagnosed. Its wrong cells are excluded and reported as failures |
| The paper can explain the scale-free and GAPBS losses | Scale-free: yes (RIKEN ~3× with its phases pruning work; ACIC's work per edge, 8a). **GAPBS: one-node GAPBS beats 8-node ACIC on the high-diameter graphs (road-usa-z 7.5×)** -- explainable (per-node re-work) but it caps the claim at "among distributed systems" |
| Adaptive vs strong fixed | Under the author's definition (§1b), adaptivity is supported by C3a (event-driven flow control, 12–22×) and C3b (read-time decisions ≈ per-graph tuning). C3c (live feedback on a parameter beats a constant) is not supported yet; L3 targets it. The title keeps "adaptive" in the §1b sense, distinct from the 2024 title |
| One-node runs no faster than 8-node ACIC (added 09-18) | **Not met**: road-usa-z 5.8–7.5×, mesh24-z 2.1×, mesh26-z 1.7× behind one-node GAPBS. [Plan](ipdps27-onenode-gap.md) |
| Result depends on the old width bug / a poorly matched baseline | No: every cell above is on the current build, with RIKEN's widened grid |

What remains for 09-26: the 8-node C3 test and E1 second allocation, the
high-diameter E3 (partial if it times out), and the decision itself, which
turns on whether a distributed-only high-diameter claim with a stated
one-node GAPBS gap is a paper the authors want to submit.

## 6. Budget and calendar

Balance on 09-18: 66.1 K SU. Planned sprint spend ≤ 12 K SU: E1 ~5.6 K, E3
~3 K, E2 and diagnoses ~0.5 K, reserve for one diagnosed change ~2 K.

| Date | Work |
|---|---|
| 09-18/19 | E4 and E2 measurements; E1 smoke then first allocations; this document and the paper skeleton |
| 09-20/21 | E1 second allocations; E3; E2 reading and at most one change |
| 09-22–25 | Change A/B (if any); tables; freeze build and data |
| 09-26 | Go/no-go against §5's rules |

**Revised 09-18 (evening)** for the one-node requirement. Spend so far is
about 10.3 K SU. The schedule and its ~7 K SU are in
[ipdps27-onenode-gap.md §6](ipdps27-onenode-gap.md#6-schedule-and-stop-rule):

- D0 and L1 on 09-19;
- L2 on 09-20–23, with the multi-source harness built alongside;
- L3 on 09-23–24;
- freeze on 09-25;
- go/no-go on 09-26, on the one-node condition first;
- E1 and E3 re-takes on the frozen build on 09-27–30.

The projected total is ~17–18 K SU, against 55.8 K remaining.
