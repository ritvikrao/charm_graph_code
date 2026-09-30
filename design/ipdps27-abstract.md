# IPDPS 2027 abstract (draft, 2026-09-30)

Submission limit: 500 words, due 2026-10-01 AOE. Revised from the author's
rewrite of 2026-09-30: the method is ACIC and the SSSP algorithm ACIC-SSSP;
adaptivity is emphasized; the abstract gives headline speedups per graph class,
not per mechanism. Numbers are the F8/F8w heap rows (freeze 601697e; width
ln V / 8 on low-degree inputs), current-state §33–§38.

**Title (working):** Scaling Single-Source Shortest Paths Beyond Graph500:
Adaptive Asynchronous SSSP on Meshes, Terrain and Road Networks

---

Most distributed single-source shortest path (SSSP) codes are designed and
tuned for the Graph500 benchmark, which measures performance on Kronecker
graphs with a skewed degree distribution, a small diameter and uniformly
random edge weights. Many large real-world graphs that need distributed memory
are structurally very different. Simulation meshes, 3-D grids, terrain models
and road networks have a low, uniform degree, diameters in the thousands to
millions, and physically derived edge weights spanning wide ranges.
Implementations optimized for Graph500 do not scale on such graphs, to the
point where one node running a shared-memory SSSP code outperforms them on 64
nodes.

We present ACIC-SSSP, an asynchronous distributed SSSP algorithm built on
ACIC, a general method for adaptive, introspective control of fine-grained
asynchronous computation in the Charm++ runtime. Shared-memory asynchronous
codes rely on atomic updates to shared distances and on work stealing. Across
nodes every remote update is a small message, and the aggregation that makes
such messages affordable delays the priority information the algorithm needs.
ACIC-SSSP adapts to both the input and the state of the computation. Periodic
reductions give every process a global view of the work in flight, and
broadcasts return priority thresholds that hold work back only when that view
calls for it. The same view drives message aggregation: buffers are flushed
eagerly when too little work is in flight to fill them, and left to fill when
traffic is heavy. ACIC-SSSP also deals the graph to processes in small tiles,
so that every process holds part of the moving frontier, and measures the
locality of the vertex order at load time to decide whether tiling pays. Each
mechanism builds on a component a message-driven runtime already provides
(reductions, broadcasts, aggregation, overdecomposition), making them
straightforward in Charm++ and awkward in bulk-synchronous MPI.

We evaluate ACIC-SSSP against four distributed codes (Gluon, the RIKEN
Graph500 SSSP code, HavoqGT and Gemini) and two tuned shared-memory codes
(GAPBS and Wasp) on up to 64 Frontier nodes (3,584 CPU cores). The inputs are
2-D meshes, 3-D grids, road networks of North America, Europe and
Afro-Eurasia, and Copernicus terrain with Tobler walking-time weights, up to
34 billion vertices and 275 billion edges. At 64 nodes ACIC-SSSP beats the
strongest distributed code on each class by 56–203× on 2-D meshes, 17–23× on
3-D grids, 17–21× on terrain and 10–85× on the largest road networks, and the
others by one to three orders of magnitude. On one node, ACIC-SSSP with its
chunked queue is within 1.4–2.3× of GAPBS and 1.7–3.1× of Wasp on meshes,
grids and terrain. From 4 nodes it is faster than both, and it keeps scaling
to 64 nodes (up to 44× its one-node speed) and to graphs no single node can
hold, solving 275 billion edges in 24–26 s. On the Kronecker graphs Graph500
uses, ACIC-SSSP is 1.7–12× faster than Gluon, Gemini and HavoqGT and within
2.4–3.1× of the Graph500-tuned RIKEN code. It stays close to specialized code
on the graphs Graph500 measures, and scales on the graphs it leaves out.

---

## Notes for revision

- Word count of the text between the rules: check with
  `awk '/^---$/{f=!f; next} f' design/ipdps27-abstract.md | wc -w` (limit 500).
- Changes from the author's rewrite, and why:
  - "ACIC almost approaches the performance of the RIKEN code": RIKEN is
    2.4–3.1× faster on RMAT (ACIC 0.32–0.42×, 16 nodes). Stated as "within
    2.4–3.1×", with the 1.7–12× over Gluon, Gemini and HavoqGT.
  - "8–240× over Gluon": the measured range is 8.04–246×. It is replaced by
    the per-class 64-node numbers, which are the headline.
  - "flushes messages based on thread activity": the flush that matters is
    starvation-gated, driven by the controller's global view of work in flight
    (1.4–2.2× at 64 nodes, F11/F13). The idle-flush settings are inert at
    scale (§37), so the sentence ties flushing to the reductions.
  - "skewed edge weights over a large range": true of the road and terrain
    weights but not of the synthetic meshes. Now "physically derived edge
    weights spanning wide ranges".
  - Thresholds: "hold work back only when that view calls for it" is
    accurate. The histogram threshold is at the top bucket in 89–99% of rounds
    (F10, §35); it is a safeguard, not a source of speedup.
- **Pending (§38):** "measures the locality of the vertex order at load time
  to decide whether tiling pays" describes `--reader-tile locality`
  (acic_frz_tloc2, auto tile or off). F15b (5573562, 5573563) validates it.
  If it fails, cut the clause. The headline numbers do not depend on it: on
  every Morton-ordered input the rule chooses what `auto` chose.
- Headline numbers (64 nodes, heap, per source; the strongest distributed
  code per class in brackets):
  - 2-D meshes (64 nodes, strongest distributed code): 56–203× (Gluon).
  - 3-D grids: `grid3-30-z` 17.4–21.3× and `grid3-33-z` 19.9–22.9× (Gluon).
  - Terrain: `terrain30-s-z` 17.1–20.9× (HavoqGT). The larger terrains have
    only Gluon lower bounds (≥ 109–119×).
  - Largest roads (`road-na-z`, `road-eu-z`, `road-planet-z`): 10.0–84.7×
    (Gluon).
  - **One node against one node** (revised 2026-09-30; the author objected to
    comparing 64 nodes of ACIC-SSSP with one node of GAPBS and Wasp). Chunk
    queue, width ln V / 8, on `mesh26-z`, `mesh28-z`, `mesh30-z`,
    `grid3-30-z` and `terrain30-s-z`:
    - One node: 0.44–0.74× of GAPBS (so GAPBS is 1.35–2.3× faster) and
      0.32–0.60× of Wasp (1.7–3.1×).
    - 4 nodes: 1.35–2.77× faster than GAPBS and 1.01–1.92× faster than Wasp.
      `mesh30-z` is the narrowest (1.01–1.12×).
    - 1 → 64 nodes: 3.8× (`mesh26-z`), 8.1×, 15×, 44× (`grid3-30-z`) and 21×
      (terrain).
    - Excluded from "meshes, grids and terrain":
      - `mesh28-w10-z` and `mesh28-w64k-z`, where the chunk queue loses (heap
        at one node 0.27–0.37× of GAPBS);
      - `mesh24-z`, which is too small to scale;
      - the roads, which have no chunk arm. With the heap they are 0.38–0.57×
        of GAPBS and 0.20–0.27× of Wasp on one node, faster than GAPBS from
        4–16 nodes, and tie Wasp from 16–32.
    - With the heap (the headline configuration elsewhere) the one-node gap is
      2.7–4.3× to GAPBS and 3.2–7× to Wasp. It passes GAPBS at 4–16 nodes and
      Wasp at 16.
    - "Graphs no single node can hold": `grid3-33-z` (893 GB file),
      `terrain30-m-z` and `terrain30-l-z` (275B edges) exceed a Frontier
      node's 512 GB. `terrain30-l-z` takes 23.7–25.8 s at 64 nodes with
      chunks (heap 43.4–46.1 s).
  - Kronecker (RMAT 25–27, 16 nodes, width ln V as configured for scale-free
    inputs): Gluon 2.20–5.08×, Gemini 1.66–2.42×, HavoqGT 5.28–11.7×; RIKEN
    0.32–0.42×.
  - "One node … outperforms them on 64 nodes": for example, Wasp on one node
    against Gluon at 64 on `grid3-30-z`, and against HavoqGT at 64 on
    terrain.
- Dropped from the previous draft: the per-mechanism ablation numbers and the
  "Mind the Gap" weight sentence. The ablation now attributes the speedup to
  tiling (5.0–7.4× at 64 nodes) and starvation-gated flushing (1.4–2.2×). The
  "shared queues 5–14×" figure is withdrawn (§38).
- Double-blind: no author names or institution; "our" never refers to prior
  papers; Charm++ may be named as a public system.
