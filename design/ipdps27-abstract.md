# IPDPS 2027 abstract (draft, 2026-09-29)

Submission limit: 500 words, due 2026-10-01 AOE. Numbers are from the F8 final
matrix on the freeze binaries (601697e, heap rows; current-state §33–§37).

**Title (working):** Scaling Single-Source Shortest Paths Beyond Graph500:
Adaptive Asynchronous SSSP on Meshes, Terrain and Road Networks

---

Distributed single-source shortest path (SSSP) codes are designed, tuned and
ranked on the Graph500 benchmark: Kronecker graphs with a skewed degree
distribution, a small diameter and uniformly random edge weights. Many graphs
that need distributed memory look nothing like this. Simulation meshes, 3-D
grids, terrain models and road networks have low, uniform degree, a diameter
in the thousands to millions, and physically derived edge weights. We show
that on these graphs the Graph500-style distributed codes do not scale: one
Frontier node running shared-memory SSSP often beats 64 nodes running a
distributed code.

We present ACIC, an asynchronous distributed SSSP algorithm. Shared-memory
asynchronous codes such as Wasp rely on atomic updates to shared distances and
on work stealing; neither exists across nodes, where every remote update is a
message and aggregating messages for throughput delays the priority
information the algorithm needs. ACIC addresses the three resulting problems.
It flushes aggregated updates at a cadence that adapts to starvation, trading
bandwidth against staleness. It deals vertices to processes in small tiles, so
the moving wavefront keeps every process busy without cross-node stealing. It
shares one priority queue per process and removes work nearest-bucket-first in
batches, bounding the extra work asynchrony causes as nodes are added. Each
mechanism builds on a component a message-driven runtime already provides
(aggregation, idle-time scheduling, reductions), making them straightforward
in Charm++ and awkward in bulk-synchronous MPI.

We evaluate ACIC against four distributed codes (Gluon, the RIKEN Graph500
SSSP code, HavoqGT and Gemini) and two tuned shared-memory codes (GAPBS and
Wasp) on up to 64 Frontier nodes. The inputs are 2-D meshes, 3-D grids, road
networks of North America, Europe and Afro-Eurasia (distance and travel-time
weights), and Copernicus terrain with Tobler walking-time weights, up to 34
billion vertices and 275 billion edges. Baselines are tuned on training
sources and measured on held-out ones; every result is checked against a
reference.

On these graphs ACIC is 8–250× faster than Gluon and one to three orders of
magnitude faster than the RIKEN code, HavoqGT and Gemini. It is also the only
distributed code here that beats a tuned single node: at 64 nodes it is
5.6–16.7× faster than GAPBS and 2.9–9.0× faster than Wasp on the largest
meshes, grids and terrain, and up to 30× and 16× with a chunked queue. On
Graph500's Kronecker graphs the RIKEN code remains faster. A benchmark that
measures only one class of graph misses the other.

An ablation at 16 and 64 nodes attributes the speedup: shared queues 5–14×,
nearest-bucket removal 2.2–3.7×, batching 1.5–1.6×, tiled placement 1.7–4.4×,
starvation-gated flushing 1.2–1.7×, and a chunked queue up to 2.3× more on
most meshes, grids and terrain. Sharing queues but distributing naively is
4.8–6.9× slower at 64 nodes. A dynamic admission threshold admits all work in
89–99% of rounds and gives nothing. Recent work shows synthetic uniform
weights misrepresent shared-memory SSSP; we extend this to distributed memory,
across weights from [1, 10] to [1, 65,536] and natural road and terrain
weights. Limit: ACIC needs a locality-preserving vertex order.

---

## Notes for revision

- **Must change before submission (§38, 2026-09-30).** "Shared queues 5–14×"
  measures `--process-share off`, which also moves updates inside a process
  onto the older threshold-deferred TRAM path. 56 single-worker processes (one
  queue per core) are about as fast as the 8 × 7 shared queues (0.72–1.50× of
  its time). Paragraph 2's queue sentence and the ablation's first item
  therefore need rewriting. What F13/F14 support instead:
  - tiled placement 5.0–7.4× at 64 nodes on the mesh and terrain (tiling off
    alone);
  - a queue per 7-core L3 region beating larger shared domains by 1.1–2.9×.

- Word count of the text between the rules: check with
  `awk '/^---$/{f=!f; next} f' design/ipdps27-abstract.md | wc -w` (limit 500).
- "One Frontier node … is often faster than 64 nodes running a distributed
  one": true for Gluon, RIKEN, HavoqGT and Gemini against GAPBS/Wasp on the
  meshes, grids and terrain (e.g. `grid3-30-z`: Wasp 1 node against Gluon at
  64 nodes). Recheck against F8 before submission.
- Paragraph 2 (revised 2026-09-29) is organized around the three problems
  that do not exist in shared memory, so ACIC does not read as distributed
  Wasp. F11 (§37) confirms the flush-cadence claim at 16–64 nodes (1.2–1.7×
  over a fixed cadence on the mesh, terrain and roads). The naive-distribution
  arm is 4.8–6.9× slower at 64 nodes on the mesh and terrain, so the stop rule
  was not triggered. The idle-flush settings are inert at scale off RMAT; do
  not claim them.
- "awkward in bulk-synchronous MPI" is an argument, not a measurement. See
  the framing section of sc27-plan.md for the evidence we can add (an
  implementation comparison and an aggregation ablation).
- "adapt to the input at run time": `--process-share auto`, `--reader-tile
  auto`, `--hub-hints auto`. The reader-tiling rule mis-chooses on row-major
  meshes (O1b), which the limitations sentence covers.
- Numbers use width ln V / 8 on low-degree inputs (F8w, §37); scale-free
  inputs keep ln V. The "bucket width costs up to 2×" limit was dropped. On
  `mesh28-w10-z` at 64 nodes, ⅛× is still 1.2–1.5× behind the widest width
  tested (§36, ratio of the range ends), so the paper should say one rule is within 1.5× of the best
  width tested.
- "8–250× over Gluon" is the heap arm: 8.04× (`grid3-30-z`, 4 nodes) to 246×
  (`road-planet-z`, 4 nodes).
- Double-blind: no author names or institution; "our" never refers to prior
  papers; Charm++ may be named as a public system.
