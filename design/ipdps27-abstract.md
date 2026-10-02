# IPDPS 2027 abstract (author's version, 2026-10-02)

Submission limit: 500 words (this text: 446). The author's version of
2026-10-02 replaces the 2026-09-30 draft (in Git history). It drops the
one-node sentence and the 275-billion-edge timing, and it names the adaptive
mechanisms instead of giving per-mechanism numbers. Numbers are the F8/F8w heap
rows (freeze 601697e; width ln V / 8 on low-degree inputs), current-state
§33–§38.

**Title (working):** Scaling Single-Source Shortest Paths Beyond Graph500:
Adaptive Asynchronous SSSP on Meshes, Terrain and Road Networks

---

Most distributed single-source shortest path (SSSP) codes are designed and tuned for the Graph500 benchmark, which measures performance based on Kronecker graphs with a skewed degree distribution, a small diameter, and uniformly random edge weights. However, many real-world large graphs that cannot fit in one node’s memory exhibit structural characteristics starkly different from those of scale-free networks. In particular, simulation meshes, 3-D grids, terrain models, and road networks have a low, uniform average degree, extremely large diameters, and skewed edge weights over a large range of values. Implementations optimized for Graph500 do not scale on such graphs, to the point where one node running a shared-memory SSSP outperforms a distributed code running on 64 nodes.

We present ACIC-SSSP, an asynchronous distributed SSSP algorithm built on the Charm++ parallel runtime system. Shared-memory asynchronous codes rely on atomic updates and work stealing on shared data structures, but distributed implementations must consider the cost of large numbers of small, fine-grained update messages. ACIC-SSSP addresses this issue with an adaptive approach that reacts to both the input and the state of the computation. Asynchronous continuous reductions provide a global view of the distance updates in-flight, enabling control of the flow of messages based on this information. Message aggregation buffers are flushed eagerly if they fill up more slowly, but are left to fill up when updates are inserted more frequently. ACIC-SSSP also distributes input graphs in small tiles, so that every process holds part of the moving frontier, and measures the locality of the vertex order at initialization to decide if tiling will help performance. Each mechanism builds on existing Charm++ capabilities such as aggregation, idle-time scheduling, overdecomposition, and asynchronous broadcasts and reductions, making the implementation of ACIC-SSSP straightforward in Charm++ despite being awkward in bulk-synchronous MPI.

We evaluate ACIC-SSSP against four distributed codes and two tuned shared-memory codes on up to 64 Frontier nodes (3,584 CPU cores). The inputs are 2-D meshes, 3-D grids, road networks of North America, Europe and Afro-Eurasia (distance and travel-time weights), and Copernicus terrain with Tobler walking-time weights, up to 34 billion vertices and 275 billion edges. At 64 nodes ACIC-SSSP achieves speedups over the strongest distributed code on each class of 56–203x on 2-D meshes, 17–23x on 3-D grids, 17–21x on terrain and 10–85x on the largest road networks, and the others by one to three orders of magnitude. On scale-free graphs, ACIC-SSSP achieves speedups of 1.7-12x over most distributed codes, while coming close to a highly optimized Graph500 implementation. This work establishes ACIC-SSSP as a competitive implementation for large-scale distributed graphs regardless of input structure, and shows how parallel runtime optimizations can be used to speed up fine-grained applications such as SSSP.

---

## Notes for revision

- Word count of the text between the rules: check with
  `awk '/^---$/{f=!f; next} f' design/ipdps27-abstract.md | wc -w` (limit 500).
- **Checked against the record; for the paper, not edits to this text:**
  - "(distance and travel-time weights)": every road input uses distance.
    `road-usa` is DIMACS `USA-road-d`, and the OSM roads take RoutingKit's
    `geo_distance` in metres (`benchmarks/osm_to_dimacs.cpp:10`). Travel time
    is extracted but never used. Either drop "and travel-time" or build and
    run a travel-time road before the paper says it.
  - "skewed edge weights over a large range": true of roads and terrain. The
    meshes and grids are uniform on [1, 10], [1, 1000] and [1, 65,536]; wide
    range, but not skewed. F12 (weight-distribution fits) would back the road
    and terrain half.
  - "flushed eagerly if they fill up more slowly": accurate per destination
    (a buffer that has not filled since the last round is flushed). The policy
    also has a global gate: it acts only in rounds where the controller sees
    too little work in flight to fill the buffers. Without the gate
    (`--flush-policy stale`) it was 12% slower on RMAT. The previous sentence
    ("control of the flow of messages based on this information") covers the
    gate; the paper should state it.
  - "idle-time scheduling": supported on scale-free graphs. Turning the idle
    flush off costs 1.56–1.89× on `rmat26` at 64 nodes; on mesh, terrain and
    roads it is within 4% (F11, §37).
  - "coming close to a highly optimized Graph500 implementation": RIKEN is
    2.4–3.1× faster on RMAT 25–27 at 16 nodes (ACIC 0.32–0.42×). Reviewers
    will compare "close" with that number; the paper must give it.
  - "competitive … regardless of input structure": rests on the RMAT result
    above. The paper should present the RIKEN boundary as the regime limit
    (hop-diameter figure) so the claim is not read as a win on every input.
  - "1.7-12x" uses a hyphen where the other ranges use an en dash.
- **Pending (§38):** "measures the locality of the vertex order at
  initialization to decide if tiling will help" describes
  `--reader-tile locality` (acic_frz_tloc2, auto tile or off). F15b (5573562,
  5573563) validates it. If it fails, cut the clause. The headline numbers do
  not depend on it: on every Morton-ordered input the rule chooses what `auto`
  chose.
- **Evidence for "adaptive"** (what the paper cites):
  - Starvation-gated flushing, adapting to the state of the computation:
    1.2–1.7× over a fixed cadence on the mesh, terrain and planet roads, and
    the gain grows with nodes (mesh 1.23× → 1.44×, terrain 1.25× → 1.67×,
    16 → 64 nodes). Fixed flushing raises edge attempts per edge from 2.95 to
    4.92 on `mesh32-z` (F11, §37).
  - Adapting to the input: the same build engages different mechanisms per
    graph class. The starvation gate fires every round on meshes, and only in
    the ramp and tail on RMAT, where the buffers fill on their own. The idle
    flush matters on RMAT (1.56–1.89×) and not on low-degree graphs.
    Locality-aware tiling picks tiles or none from the vertex order (29× on
    row-major `mesh26`, F15).
  - Not cited as a source of speedup: the histogram admission threshold. It
    sits at the top bucket in 89–99% of rounds (F10) and is 1.08–1.67× slower
    when engaged (D4b). The paper calls it a safety bound.
- Headline numbers (64 nodes, heap, per source; the strongest distributed
  code per class in brackets):
  - 2-D meshes: 56–203× (Gluon).
  - 3-D grids: `grid3-30-z` 17.4–21.3× and `grid3-33-z` 19.9–22.9× (Gluon).
  - Terrain: `terrain30-s-z` 17.1–20.9× (HavoqGT). The larger terrains have
    only Gluon lower bounds (≥ 109–119×).
  - Largest roads (`road-na-z`, `road-eu-z`, `road-planet-z`): 10.0–84.7×
    (Gluon).
  - Kronecker (RMAT 25–27, 16 nodes, width ln V as configured for scale-free
    inputs): Gluon 2.20–5.08×, Gemini 1.66–2.42×, HavoqGT 5.28–11.7×; RIKEN
    0.32–0.42×.
  - "One node … outperforms a distributed code running on 64 nodes": for
    example, Wasp on one node against Gluon at 64 on `grid3-30-z`, and against
    HavoqGT at 64 on terrain.
- **The one-node comparison is no longer in the abstract.** It moves to the
  evaluation and the limitations section (plan: "Paper layout"). One-node
  numbers come from Delta's same-node D7 matrix (in progress). Frontier's
  one-node points appear only as the start of its scaling series, where
  ACIC-SSSP passes GAPBS and Wasp from 4 nodes and scales 3.8–44× from 1 to
  64 nodes.
- Double-blind: no author names or institution; "our" never refers to prior
  papers; Charm++ may be named as a public system.
