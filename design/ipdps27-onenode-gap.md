# Closing the one-node gap

*2026-09-18. Part of the [IPDPS27 sprint](ipdps27-sprint.md). This replaces the
sprint's earlier position that the one-node GAPBS gap is "reported, not
closed". **Requirement (author, 09-18):** a one-node run must not beat an
8-node ACIC run. That holds for GAPBS and for ACIC's own one-node run.*

## 1. Target

| graph | GAPBS, 1 node (s) | ACIC, 8 nodes now (s) | gap | ACIC, 1 node now (s) |
|---|---:|---:|---:|---:|
| road-usa-z | 0.118 | 0.69–0.88 | 5.8–7.5× | 1.66 |
| mesh26-z | 0.760 | 1.28–1.29 | 1.7× | 5.19 |
| mesh24-z | 0.174 | 0.37–0.38 | 2.1× | 0.917 |
| road-usa-w4-z | (not run) | 0.66 | ? | ? |
| rmat25, orkut, uniform25, rmat26, rmat27 | 0.15–3.9 | 0.10–0.74 | ACIC ahead 1.4–5× | — |

(C6 table, job 20826395; E3, jobs 20826394 and 20827653.)

**Pass:** on every high-diameter paper graph, 8-node ACIC is no slower than
one-node GAPBS: a median ratio ≤ 1.0, with no held-out source above 1.2×.
Two allocations. **Stretch:** 1.5× ahead, and one-node ACIC within 3× of
GAPBS. The scale-free graphs already pass and must not regress by more than
the allocation floor.

road-usa-z needs a **6–8× speedup at 8 nodes**. The meshes need about 2×.

## 2. Budget arithmetic for road-usa-z at 8 nodes

In 0.118 s, 896 PEs have 106 PE-seconds between them.

- **Work now.** At one node, ACIC's own work on native road-usa is 34–38
  PE-seconds, at 8.6 distance changes per vertex, which is about 175 ns per
  change (E2). At 8 nodes on road-usa-z it makes 14.7 changes per vertex.
  At the same cost per change that is roughly 60 PE-seconds, more than half
  the budget, *if* it were perfectly spread.
- **It is not spread.** The solver's own work is 10–20% of PE time on roads
  (E2, compute share). The rest is PEs waiting for work or for a round.
- **Rounds.** 1,562 rounds in 0.88 s is 0.56 ms per round. For 0.118 s at
  the same round count, a round must take 75 µs. A reduction plus broadcast
  over 8 Slingshot nodes costs 10–30 µs, so this is possible, but only if a
  round stops waiting for busy PEs.

So three things must all change, and fixing any one alone is not enough:

1. Re-work must fall from 14.7 to about 2–3 changes per vertex. GAPBS's
   Δ-stepping makes about 1.1–1.5.
2. Each wavefront's work must be spread over most PEs, not over the few that
   own the part of the map it is crossing.
3. The round must be either cheap, or not on the critical path.

## 3. Evidence for each cause

- **Wavefront concentration.** Morton order makes ACIC *slower* at one node:
  mesh24 0.548 → 0.917 s and road-usa 1.51 → 1.66 s (C6 table). At 8 nodes
  it is faster (E2). At one node the cut barely matters, but a compact
  region per PE puts each wavefront on a few PEs. H3 had already measured
  mesh PEs idle in 83–85% of controller rounds, at ≤32 PEs, in row-major
  order. At 896 PEs with compact regions, it can only be worse. Not yet
  measured on the `-z` inputs.
- **Re-work.** Morton cut it 3–10× (E2), but 14.7 changes per vertex remain
  on road-usa-z at 8 nodes and 5.5–7.6 on the meshes. Most of it is not
  caused by edges crossing PEs, because 0.63% of edges cross. Each PE runs
  ahead of the global frontier inside its admitted window, so a PE settles
  a region from a distance that is later beaten.
- **Round cost.** 0.27 ms per round at one node (road-usa, 6,282 rounds,
  1.6 s), and 0.56 ms at 8 nodes. 8c showed that the round waits for the
  slowest of 896 PEs to reach a pickup point; moving the control messages
  out of the queue did not shorten it.
- **Per-update overhead inside a node.** At one node, every edge between
  PEs, even PEs of the same process, goes through htram. That means an
  insertion, a buffer, a node-queue delivery and a per-PE split. GAPBS does
  an atomic min on a shared array.

## 4. Diagnosis first (D0, 09-19, ~150 SU)

Take one `acic_ipdps_diag` run on road-usa-z, mesh24-z and mesh26-z at 1, 2
and 8 nodes, plus the native orders at one node. Record:

- per round: how many PEs processed any update (`idle_rounds` exists per PE,
  and must be made per round), the window, and the round time;
- distance changes per vertex, split into changes caused by a cross-PE
  update and changes caused by a same-PE one (a new counter);
- the solver's own work share (`acic_8g_comm`);
- GAPBS's number of buckets and its time per bucket on the same graphs (a
  counter in `gap_driver.cpp`), as the reference for how many "rounds"
  road-usa needs.

The output is a single table that apportions ACIC's 0.69–0.88 s among idle
PEs, rounds, re-work and cost per change. The levers below are ordered by
the expected answer. D0 can reorder them.

## 5. Levers, in order

Every lever is a runtime or read-time decision built on a Charm++
abstraction, which is what the paper is about (sprint doc §1b). Each one
gets the verify gate and a paired A/B against the frozen build in two
allocations before it is adopted.

### L1. Tiled vertex placement (over-decomposition by tiles)

Cut the Morton order into tiles of T vertices and deal them round-robin to
PEs, so that every PE owns many small compact tiles spread over the whole
map. Each wavefront then crosses tiles of every PE. The cut grows with the
tile perimeter: for a mesh tile of 4,096 vertices, about 3%, against 1% for
plain Morton at 896 PEs and 11% for row-major. So T trades re-work against
spread.

- **First as a relabeling** (`reorder_graph.py --tile T --pes P`; 1 day).
  It needs no solver change and tests the idea. Sweep T over
  {V/P (plain Morton), V/4P, V/16P, V/64P} at 1, 2 and 8 nodes on the three
  graphs. The file depends on P, and every system gets the same file.
- **Then in ACIC's reader, if it pays.** T is chosen when the graph is read,
  from V, P and the degree. The destination table already maps a vertex to
  its PE in one load, so a tiled map costs nothing per update. This is
  over-decomposition, the partition as many pieces per PE, which the 2024
  paper named as future work. It is also a data-dependent decision made
  when the graph is read.
- **Expected:** it removes most of the idle PEs, and it should reverse
  Morton's one-node slowdown. It does not reduce re-work, and may add some.

### L2. Shared state within a process

The 15 PEs of a process share an address space (Charm++ SMP). Make the
process, not the PE, the unit that owns vertices:

- Relaxations to a vertex owned by the same process do an atomic min on the
  process's distance array and push the vertex into its owner's queue. They
  do not go through htram. Only edges between processes are aggregated.
- The PEs of a process drain a shared, bucketed work structure (per-PE
  bins merged per bucket, as GAPBS does with threads), so a wavefront that
  hits one process is worked by all 15 of its PEs. This is the intra-process
  work sharing that the 2024 paper named as future work.
- With L1's tiles owned by processes, a process holds 15 PEs' worth of
  tiles, and the cut between processes is 15 times smaller.

Engineering: 4–6 days. The owner-computes path through `process_heap` and
htram's per-PE delivery stays for edges between processes. Risk: Reconverse's
node-queue lock (4.9× at 120 PEs per process; 15 per process is the regime
already in use). **Expected:** at one node this makes ACIC a shared-memory
code for 1/8 of its edges, plus htram between 8 processes. It is the
largest single step toward GAPBS's cost per change.

### L3. Live control of how far ahead a PE may run

The histogram controller admits a window, and a PE processes anything inside
it. Replace the fixed percentile with a slack that the controller sets every
round from what it measures. It tightens when the distance changes per
retired update rise, which means re-work, and loosens when the fraction of
idle PEs rises, which means starvation. The PE side stays asynchronous:

- inside the slack it processes without waiting for a round;
- outside it, it holds.

This targets re-work (cause 1) and makes the round a regulator, not a gate
(cause 3). It is flow control from live state, the adaptivity the paper
claims (§1b). Engineering: 2–3 days on the existing controller.

### L4. Cheaper rounds (only if D0 says rounds still bind after L1–L3)

- Summing contributions per process (8c's `ControlNode`) together with
  L2's shared state, so a round needs one ready PE per process, not all 15.
- Prioritized control messages: Charm++ message priorities, not a separate
  queue.
- 8c's result, that the round waits for the slowest PE, is why this comes
  last.

### Not pursued

- **2D or 1.5D partitioning.** It suits scale-free graphs; roads and meshes
  are sparse and near-planar.
- **Bigger inputs as a substitute for closing the gap.** mesh28 and a larger
  road graph belong in the paper as additional rows, because the gap
  narrows with size (mesh24 2.1× → mesh26 1.7×). But road-usa stays, and
  it must pass.
- **Weakening GAPBS.** It keeps its tuned Δ, its best thread count and
  bucket fusion.

## 6. Schedule and stop rule

| Date | Work | SU |
|---|---|---:|
| 09-19 | D0; L1 as a relabeling, the T sweep at 1/2/8 nodes | ~500 |
| 09-20–23 | L2, the verify gate, and an A/B at 1/2/8 nodes | ~800 |
| 09-23–24 | L3, the verify gate, and an A/B | ~500 |
| 09-25 | L1 in the reader if the relabeling paid; freeze the build | ~200 |
| **09-26** | **Go/no-go:** 8-node ACIC ≤ one-node GAPBS on road-usa-z, mesh24-z and mesh26-z in one allocation, and ≤ 1.2× in the worst held-out source | — |
| 09-27–30 | Second allocation of the pass test. Re-take E1 (the arms that mattered) and E3's ACIC cells on the frozen build | ~5,000 |
| 10-01 | Abstract | — |

**If it does not pass on 09-26, IPDPS is a no-go.** The work continues, and
it becomes the SC27 plan's first milestone. It is not submitted as a
distributed-only claim.

The re-takes after 09-26 need the harness to solve several sources per
launch. At the current 6–8 s launch overhead per run, E1's two allocations
alone cost ~5,600 SU. Build it during L2 (09-20–23), so that it is ready
before the freeze.

**Sprint rules replaced:** the sprint's "at most two diagnosed changes"
rule does not survive this requirement. L1–L3 are three more. Each still
needs its own paired A/B in two allocations, and the paper reports each as
a separate step, with the one-node run in the same table.
