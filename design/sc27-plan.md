# ACIC forward plan

*Decision plan from 2026-09-23; the IPDPS 2027 submission plan (2026-09-28)
supersedes its schedule. Earlier schedules remain in Git history. Read
[current-state.md](current-state.md) and [configurations.md](configurations.md)
before acting on this plan.*

## Status

ACIC has a reproducible result on one graph class: at 896 CPU PEs it solves
`mesh26-z` 1.21–1.43× faster than tuned one-node GAPBS, across Anvil and
Frontier, four held-out sources and two allocations per system. Heap slicing
is causal: without it the result falls to parity or worse. Road remains about
1.5× behind GAPBS, scale-free graphs remain behind RIKEN, and a live adaptive
policy has not caused the current win.

Road does not have to beat a shared-memory implementation on the current graph
to become a successful result. If ACIC remains close on the largest common
input and then scales exact weighted SSSP beyond single-node memory, that is a
capacity and scalability contribution. It must be claimed separately from a
same-input performance win.

The next work should close the current causal chain, freeze SSSP and write.
Do not start BFS, PageRank, a generic payload refactor or a broad parameter
sweep during the IPDPS decision window.

## IPDPS 2027 submission plan (2026-09-28)

*This section supersedes the September 23 schedule below ("Work through
September 26" and "September 27–October 8 if the gate passes"). The user chose
IPDPS on 2026-09-28; SC27 remains the fallback, or the venue for a version
that goes beyond SSSP.*

### Focus

Exact weighted SSSP that scales on non-scale-free graphs with varied degree
and weight ranges: the graph classes Graph500's Kronecker SSSP kernel does not
exercise. The input matrix spans:
- **Average degree:** about 2.5 (roads), 4 (2-D meshes), 6 (3-D grids), 8 (terrain).
- **Weights:** uniform [1, 10], [1, 1000] and [1, 65,536]; road distances (DIMACS `USA-road-d` and OSM `geo_distance`, metres); Tobler walking times on terrain. No input uses road travel times: `osm_to_dimacs` extracts them and does not use them.

Two results need an explanation, not a win:
- **One node, against GAPBS and Wasp.** Present it COST-style: the node count
  at which ACIC passes the best one-node code on each input, plus the honest
  one-node gap. Past one node's memory, weak scaling and the distributed
  baselines carry the argument.
- **Scale-free graphs, against RIKEN.** This is the regime boundary. One
  figure plots ACIC's speedup over RIKEN and Gluon against hop diameter
  (max distance / mean edge weight). Low-diameter RMAT sits where RIKEN, which
  is co-designed for Graph500, wins (0.29–0.90×). Meshes, grids, terrain and
  roads sit where ACIC's lead reaches 100–1000×.

### Paper framing (2026-09-29)

The user's three goals, and how the evidence supports each. The abstract
(author's version, 2026-10-02) is `design/ipdps27-abstract.md`; its notes list
the phrases to check against the record.

**Thesis.** Distributed SSSP has been designed and ranked on one graph class,
Graph500's Kronecker graphs with uniform weights. On the graphs that
simulation, geoscience and routing actually distribute (low degree, high
diameter, physically derived weights), those codes do not scale, and a single
node beats them. Asynchronous, message-driven SSSP with adaptive fine-grained
scheduling and aggregation fills that gap. A message-driven runtime is the
natural place to build it.

#### Pillar 1: an unmet need beyond Graph500

- **Claim.** Graph500-style distributed codes lose to a tuned single node on
  meshes, grids, terrain and roads. ACIC is the only distributed code in the
  study that beats one, and it keeps scaling to 64 nodes.
- **Evidence we have.**
  - Wasp on one node is faster than Gluon, RIKEN, HavoqGT and Gemini at 64
    nodes on the meshes, grids and terrain. For example, on `grid3-30-z` Wasp
    takes about 5.5 s, against Gluon's 11–14 s and RIKEN's 20–31 s at 64 nodes.
  - ACIC is 8–240× faster than Gluon and 17–2,200× faster than RIKEN, HavoqGT
    and Gemini on these inputs (dataset tables).
  - At 64 nodes ACIC is 5.0–14.5× faster than GAPBS and 2.3–7.9× faster than
    Wasp on large meshes, grids and terrain. This is the COST argument, the
    GAP suite's own rule for distributed frameworks.
  - The inputs reach 34B vertices and 275B edges, past any one node.
- **The regime boundary is part of the claim.** On RMAT and `uniform25`, RIKEN
  wins (ACIC 0.29–0.90×). The figure plots speedup against hop diameter:
  Graph500 measures one end of the axis. Say "graphs with spatial locality and
  high diameter", not "non-scale-free".
- **Pending:** F8 (final matrix); native-order Gemini, HavoqGT and RIKEN, so
  that "each code at its best order" is measured.
- **Weak points:**
  - The vertex-order assumption (O1: 107–183× on row-major `mesh26` with tiling
    auto).
  - On roads ACIC only ties or modestly beats Wasp (`road-usa-z`: Wasp wins at
    every node count; `road-planet-z` at 64 nodes 1.11–1.42×).
  - The paper says both plainly.

#### Pillar 2: adaptive fine-grained techniques, and why a message-driven runtime

*Revised 2026-10-02 after F11, F13–F15 and the author's abstract. The
earlier version credited process sharing (5–15×) and named F11 as missing.*

- **Claim (the abstract's).** ACIC-SSSP adapts to the state of the
  computation and to its input. Continuous asynchronous reductions give every
  process a global view of the updates in flight, and that view decides when
  aggregation buffers are flushed. The vertex order, measured at load time,
  decides whether the graph is dealt out in tiles. Each part uses something a
  message-driven runtime already has (TRAM/htram aggregation, scheduler idle
  hooks, overdecomposition, asynchronous reductions and broadcasts), so the
  application's policy stays small.
- **Evidence that it adapts.** The thresholds are not part of it.
  - **State: starvation-gated flushing** (F11, §37; F13, §38). In rounds
    where the controller sees too little in flight to fill the buffers, each
    destination buffer that has not filled since the previous round is
    flushed; otherwise buffers fill.
    - Against a fixed cadence at 16 / 64 nodes: `mesh32-z` 1.22–1.23× /
      1.38–1.47×, `terrain30-s-z` 1.23–1.27× / 1.66–1.69×, `road-planet-z`
      1.45–1.49× / 1.42–1.49×.
    - The gain grows with nodes on the mesh and terrain.
    - The cost it removes is stale priorities, not bandwidth: fixed flushing
      raises attempts per edge from 2.95 to 4.92 on `mesh32-z` at 64 nodes.
    - The global gate is what makes it adaptive. The same per-destination rule
      without the gate (`--flush-policy stale`) cost 12% on RMAT (step 7),
      since there buffers fill on their own for most of the run.
  - **Input: one build engages different mechanisms per graph class** (F11).
    - The idle flush costs 1.56–1.89× when off on `rmat26` at 64 nodes and is
      within 4% on the mesh, terrain and roads.
    - Small buffers cost 1.6–3.4× on `rmat26` and nothing on low-degree
      graphs.
  - **Load time: locality-aware tiling** (F15, §38; F15b, §40). The revised
    rule turns tiling off on row-major `mesh26` (21–92× faster than
    always-tiling, as fast as off) and keeps auto's tiles on DIMACS
    `road-usa` and on Morton inputs, at 16 and 64 nodes. The headline runs
    used `auto`; the rule chooses the same layout on every Morton input.
  - **Tiling itself is placement, not adaptation**, and it is the largest
    distributed mechanism: off costs 6.9–7.4× on `mesh32-z`, 5.0–6.8× on
    terrain and 1.4–1.8× on roads at 64 nodes (F13). The adaptive part is the
    rule that turns it on or off.
  - Within a node: the chunk queue (about 2× on meshes and terrain), and one
    shared queue per L3 region (F14: larger domains are 1.1–2.9× slower).
- **Weak points.**
  - On `rmat26` fixed flushing is 1.08–1.19× faster than the gated policy at
    16 nodes, and between 1.25× faster and 1.10× slower at 64 (F11): the gate
    does not fully close where buffers fill.
  - The idle-flush interval `auto` and the starved idle flush show no effect
    on low-degree graphs at 16–64 nodes; their one- and two-node gains
    (1.09–1.12×, about 2× on `road-usa` at two nodes) are older code.
- **Gate A** (stop calling it adaptive unless a live policy wins) is met by
  the flush policy, a live state-driven policy worth 1.2–1.7× at scale. It
  still rules out the admission and width rules.
- **What we must not claim.**
  - The histogram admission threshold as a source of speedup. It is at the
    top bucket in 89–99% of rounds (F10) and 1.08–1.67× slower when engaged
    (D4b, F10). The paper calls it a safety bound.
  - The buffer-size acceptance policy: it made no changes in the F10 logs.
  - "Shared queues 5–14×": withdrawn (§38). `--process-share off` is a
    different code path (threshold-deferred TRAM inside a process), so F10's
    sharing arm does not measure queue count.
  - That MPI cannot do this. HavoqGT is asynchronous and aggregates messages
    in MPI, and its author (Pearce) is on the Algorithms PC.
- **Defensible form of "easier in a message-driven runtime".**
  - Argue from the measured mechanisms: tiled placement, which needs
    overdecomposition, and the gated flush, which needs a global view
    delivered asynchronously alongside the work. Do not argue from sharing.
  - Gluon is bulk-synchronous, so thousands of rounds on high-diameter graphs
    cost it directly.
  - Back it with I1: the lines of code and runtime components each ACIC
    mechanism uses, against the equivalent code in HavoqGT (mailbox,
    termination detection) and Gluon (sync substrate). No allocation needed.

#### Pillar 3: the "Mind the Gap" recommendations

The source is D'Antonio, Mai and Vandierendonck, "Mind the Gap: The Disconnect
Between Synthetic and Natural Edge Weights in Parallel Single-Source Shortest
Path", arXiv:2607.26821 (v2, 2026-09-01). Their study is shared-memory only:
GAPBS, GBBS, Δ*-stepping, ρ-stepping, Wasp, MultiQueue and Bellman-Ford on
17 graphs, 8 of them roads. They find that uniform synthetic weights change the
tuned Δ by orders of magnitude and can reverse the ranking of algorithms, and
that asynchronous codes (Wasp, MultiQueue) are the most robust. Their three
recommendations, and our response to each:

1. **Users: tune threshold parameters on the target graph's own weight
   distribution.**
   - We do this for every baseline: GAPBS, Wasp and Gluon Δ searches on
     training sources, RIKEN's delta ratio per graph, and F7's Gluon delta
     check.
   - ACIC is not exempt. Its bucket width is a rule (ln V; a fixed 131072 on
     roads), and F5 shows the rule costing about 2× on the [1, 10] mesh. The
     width check (5568640, 5568642) tells whether one multiple works across
     inputs. If it does, adopt it; if not, report the sensitivity next to the
     baselines'.
2. **Performance analysts: evaluate on natural weights, or on synthetic
   weights that mirror them (log-normal bodies).**
   - Roads (DIMACS and OSM distance in metres) and terrain (Tobler walking time
     on Copernicus DEM) are natural weights; the meshes span uniform [1, 10],
     [1, 1000] and [1, 65,536].
   - We extend their finding to distributed memory and to topology: Graph500's
     gap is both the uniform weights and the Kronecker structure.
   - Cheap addition (F12): characterize our natural weight distributions with
     their method. If the bodies are log-normal as theirs are, add one
     log-normal-weighted mesh (`mesh28-ln-z`, fitted to the road weights) to
     the matrix.
3. **Algorithm designers: decouple efficiency from parameter choice; asynchrony
   is more robust; move toward parameter-free or dynamic thresholds.**
   - ACIC is asynchronous at distributed scale, which supports their
     robustness finding: [1, 65,536] weights cost ACIC nothing.
   - Our negative result sharpens their advice. A dynamic, histogram-derived
     admission threshold (the ρ-stepping idea) is inert under asynchrony and
     slower when forced on. What carries the performance is scheduling
     structure (shared queues, nearest-bucket batching) and adaptive
     aggregation.
   - The remaining parameter, the bucket width, is exactly where the
     sensitivity they warn about still shows.

#### Contributions, as the introduction would list them

*Revised 2026-10-02 to match the author's abstract.*

1. A characterization showing that Graph500-style distributed SSSP codes
   (RIKEN, HavoqGT, Gemini, Gluon) lose to a tuned single node on graphs with
   spatial locality and high diameter: meshes, 3-D grids, terrain and roads.
2. ACIC-SSSP: asynchronous distributed SSSP that adapts to the state of the
   computation and to its input. Continuous reductions give a global view of
   updates in flight, which gates message flushing; the graph is dealt out in
   tiles chosen by a load-time locality rule; inside a process, shared
   nearest-bucket batched queues and a chunk queue. It has a correctness and
   termination argument, the Algorithms-track requirement.
3. An evaluation at up to 64 Frontier nodes against four distributed and two
   shared-memory codes. Over the strongest distributed code per class:
   56–203× on 2-D meshes, 17–23× on 3-D grids, 17–21× on terrain and 10–85×
   on the largest roads. On Kronecker graphs, 1.7–12× over Gluon, Gemini and
   HavoqGT, with RIKEN 2.4–3.1× faster as the regime boundary. Faster than
   tuned GAPBS and Wasp from 4 nodes on meshes, grids and terrain, and
   solving inputs past one node's memory (275B edges).
4. An ablation at scale that attributes the speedup to tiled placement
   (5.0–7.4× on meshes and terrain at 64 nodes, 1.4–1.8× on roads) and
   state-adaptive flushing (1.2–1.7×, growing with nodes), and shows the
   histogram threshold inert. This extends "Mind the
   Gap" from shared to distributed memory.

**Width configuration (2026-09-29).** The paper's configuration is one rule:
bucket width ln V / 8 when the graph has fewer than 8 edges per vertex, ln V
otherwise, and a fixed 131072 on the roads. It is the width check's winner on
low-degree inputs (§36) and F8w's result on scale-free ones (§37). The
freeze binaries take it as `--bucket-width`. On every input tested it is
within 1.5× of the best width tried (`mesh28-w10-z` at 64 nodes is the
largest gap).

#### What to pursue next (none submitted; each needs the user's go-ahead)

| # | Experiment | Why | Cost |
|---|---|---|---|
| F11 (done, §37) | Aggregation ablation at 16 and 64 nodes on the freeze heap: default (adaptive flush, starved idle flush, interval auto) against `--flush-policy fixed`, `--idle-flush off`, `--idle-flush on`, `--idle-flush-interval 0`, and a small fixed `--bufsize`. Also a **naive-distribution arm**: the Wasp-like core kept (shared, nearest-bucket, batched queues) with every distributed mechanism off (`--flush-policy fixed --idle-flush off --reader-tile off`). Inputs `mesh32-z`, `terrain30-s-z`, `road-planet-z`, `rmat26` | Pillar 2 has no evidence at scale for aggregation. The naive arm is the measurement behind "more than distributed Wasp": if it comes within about 1.2× of ACIC at 64 nodes, the abstract's second paragraph must change | 2 jobs, about 1 h each |
| F12 | Weight-distribution characterization of road and terrain inputs (log-normal and power-law fits, as "Mind the Gap" does); optionally generate and reference `mesh28-ln-z`, then ACIC, Gluon, GAPBS and Wasp on it | Pillar 3 recommendation 2 | Characterization on a login or debug node; the mesh adds 3–4 short jobs |
| I1 | Implementation comparison: lines of code and runtime components for sharing, aggregation, flushing and termination in ACIC against HavoqGT and Gluon | Pillar 2 "easier in a message-driven runtime", in a form Pearce will accept | No allocation |

### Paper layout (2026-10-02)

*Replaces the ten-page budget and four-figure plan of the September 23 schedule
(P3 and the submission checklist below), which were written for a
diagnosis-first story. Built on the author's abstract of 2026-10-02.*

Ten pages in IEEE two-column format, figures and tables included; references
do not count. No appendix at submission. Budget about 9.65 pages and leave the
rest for figure placement.

| § | Section | Pages | CFP criterion it answers | Content |
|---|---|---:|---|---|
| 1 | Introduction | 1.25 | Motivation; key contributions | The Graph500 gap; Fig. 1; the four contributions (above) with flagship numbers; one sentence on the regime boundary |
| 2 | Background and the compared codes | 0.75 | Limitations of the state of the art | Δ-stepping; what RIKEN, HavoqGT, Gluon and Gemini assume (low diameter, uniform weights, bulk rounds or per-core ranks); GAPBS and Wasp; the IA³@SC24 predecessor and exactly what is new. Table 1. Breadth goes in §7 |
| 3 | Key insights | 0.75 | Key insights; novelty; challenges | Why high-diameter, low-degree graphs break Graph500 designs: a narrow moving frontier and thousands of rounds. The insights: (a) aggregation delays priority information, so flushing must follow global work in flight; (b) the frontier must be spread over every process, so tile the graph, but only when the vertex order has locality; (c) a global view can be kept current asynchronously, without rounds. Each maps to a mechanism in §4 |
| 4 | ACIC-SSSP | 1.75 | Key insights; Algorithms-track requirement | Pseudocode; the controller's continuous reductions; the gated flush; locality-aware tiling; shared nearest-bucket batched queues and the chunk queue; the admission threshold as a safety bound; correctness and termination (monotone frontier, collective emptiness). Fig. 2 |
| 5 | Methodology | 0.75 | Methodology | Machines; Table 2 (inputs); equal PEs/threads in every comparison (on one node, the baselines run as many threads as ACIC has worker PEs, and only Δ is tuned); every baseline tuned on training sources and frozen for held-out ones; digest checks on every solve; the timing boundary (load excluded, stated per code); the same vertex order for every code. Cite the prior evaluations that used the same protocol (GAPBS, "Mind the Gap") |
| 6 | Evaluation | 3.25 | Flagship results | 6.1 Distributed codes at 16 and 64 nodes, Fig. 3. 6.2 One node against GAPBS and Wasp on Delta (D7), Table 3; scaling and the crossover on Frontier, Fig. 4. 6.3 Past one node's memory (275B edges; small inline table). 6.4 Why it wins: ablation, work and traffic per edge, Fig. 5 |
| 7 | Related work | 0.6 | Limitations of the state of the art (breadth) | Grouped by area: algorithms, shared-memory CPU, distributed CPU, GPU, runtime and aggregation, placement and order; each group ends with how ACIC-SSSP differs. Table below |
| 8 | Limitations | 0.4 | Limitations of the approach | Its own titled section, so reviewers find the criterion. List below |
| 9 | Conclusion | 0.15 | — | — |

**Figures and tables** (five figures, three tables, plus the small §6.3 table):
- **Fig. 1 (teaser).** Per class, 64-node times of the distributed codes
  against one-node GAPBS and Wasp. It is the motivation: one node beats them.
- **Table 1.** Codes by design feature: asynchrony, aggregation and its flush
  rule, queue sharing, placement, priority order. Verify every cell against the
  code or the paper, since Pearce and the Gluon authors will check theirs.
- **Fig. 2.** One process: shared queues, tiles, aggregation buffers, and the
  reduction/broadcast loop that gates flushing.
- **Table 2.** Inputs: vertices, edges, mean degree, weight range and kind,
  hop diameter, bytes.
- **Fig. 3.** Speedup over the strongest distributed code against hop
  diameter, 16 and 64 nodes, RMAT included below 1× against RIKEN. It carries
  both the headline and the regime boundary.
- **Table 3 (Delta, D7).** One node, same node for every code: ACIC-SSSP's
  paper configuration against GAPBS and Wasp, both tuned on that node, on the
  eight D7 inputs, per held-out source. This is the paper's one-node
  comparison (user, 2026-09-28: one-node runs come from Delta).
- **Fig. 4 (Frontier).** Time from 1 to 64 nodes per input. Its one-node
  points are part of the scaling series, so they are Frontier runs. One-node
  GAPBS and Wasp appear as horizontal lines so the crossover node count
  (COST style) is read on one machine; if the user prefers, drop the lines
  and give the crossover in the text.
- **Fig. 5.** Ablation at 64 nodes: tiling off, fixed flush, naive, idle flush
  off, threshold engaged. Second panel: attempts per edge and rounds, which
  show the costs are staleness and idle processes, not bandwidth.

**Where the adaptivity claim is made.** §3 states it as an insight, §4 gives
the mechanism, and §6.4 measures it: the gated flush (1.2–1.7×, growing with
nodes), the per-class behaviour (idle flush on RMAT only), and the tiling rule
(F15b). §4 presents the threshold as a safety bound, and §6.4 shows it inert.
See Pillar 2.

**The one-node comparison** is no longer in the abstract. It comes from
Delta: D7 in §6.2 (Table 3) and §8. Frontier contributes one-node points only
as the first point of its scaling series (Fig. 4).

**Related work (§7)**, added 2026-10-02. §2 and §7 have different jobs:
- §2 covers the codes the paper measures (RIKEN, HavoqGT, Gluon, Gemini,
  GAPBS, Wasp): their design assumptions, and Table 1. It answers the CFP's
  "limitations of the state of the art" with work we compare against.
- §7 gives the breadth, grouped by area. Each group ends with one sentence on
  how ACIC-SSSP differs.

The entries are candidates. **Every one must be verified against the
published paper before it is cited** (authors, title, venue, year, and what we
say it does). The CFP treats inaccurate references as grounds for rejection.
Mark the last column when done.

| Group | Candidate works | How ACIC-SSSP differs | Verified |
|---|---|---|---|
| Algorithms | Dijkstra; Bellman-Ford; Δ-stepping (Meyer and Sanders); radius stepping; Δ*-stepping and ρ-stepping; the recent sequential "sorting barrier" result | A Δ-stepping-family algorithm with asynchronous, globally informed admission instead of synchronized phases | — |
| Shared-memory CPU | GAPBS; Galois; Ligra, Julienne and GBBS; GraphIt's priority extensions; MultiQueues; Wasp; "Mind the Gap" (D'Antonio, Mai and Vandierendonck, arXiv:2607.26821) | These are the one-node references. They rely on atomics on shared state; ACIC-SSSP carries their queue structure (shared queues, nearest-bucket removal) into distributed memory. "Mind the Gap" is shared-memory only; we extend its weight argument to distributed memory and topology | — |
| Distributed CPU | Graph500 SSSP and the RIKEN code; Chakaravarthy et al. (IPDPS 2014); HavoqGT; Gluon, D-Galois and Gluon-Async; Gemini; PBGL; Distributed Control (Kanewala, Zalewski, Lumsdaine); Firoz et al. (IPDPS 2018); Pregel-style systems | Most were designed or evaluated on low-diameter Kronecker graphs, with bulk rounds or one rank per core | — |
| GPU | Davidson et al. (near-far); Gunrock; ADDS. Multi-GPU: Groute; Lux; Gluon's GPU backend | Not compared. The paper's scope is CPU-only distributed SSSP (Limitations) | — |
| Runtime and aggregation | Charm++; TRAM; Active Pebbles and AM++; Conveyors; YGM; HClib Actor | Aggregation libraries fix the flush policy; ACIC-SSSP's flush is gated by a global view of work in flight | — |
| Placement and order | Space-filling-curve orderings; CuSP (Gluon's partitioner); streaming partitioners | Tiles are chosen by a load-time locality rule (F15b) | — |
| Predecessor | The IA³@SC24 ACIC paper, cited in the third person | An explicit statement of what is new | — |
| Out of scope | Dynamic SSSP (Khanda, Bhowmick) | One line | — |

Notes for writing §7:
- **GPUs.** Frontier's nodes have GPUs, so "why not GPUs?" is a likely
  reviewer question. Without a measurement the paper cannot say GPUs lose.
  State the scope (CPU-only distributed SSSP) and list it in Limitations. The
  smaller per-node GPU memory would sharpen the capacity regime, but it is
  unmeasured here, so do not rest an argument on it. Gluon's GPU backend is the
  most likely "you could have compared" point.
- **PC members' own work.** Cite it exactly: HavoqGT (Pearce), Chakaravarthy
  et al. (Checconi), "Mind the Gap" and possibly Wasp (Vandierendonck), Firoz
  et al. (Firoz). See "Expected reviewers".
- **Madduri et al.** is listed in this plan as distributed prior work. It may
  have run on the shared-memory Cray MTA-2; check before placing it in a
  group.
- References do not count toward the ten pages, so breadth costs no page
  budget; the prose does.

**Limitations (§8)**, with the conclusions each one affects:
- **One-node gap** (Delta, D7, current-state §41; 112 PEs and threads).
  ACIC-SSSP ties or beats GAPBS on six of eight inputs (1.01–1.23× slower on
  `mesh26-z`). Wasp is faster on seven of eight: 1.04–1.25× on `rmat25` and
  1.4–2.3× on the meshes, grid and roads. ACIC-SSSP is 1.10–1.15× faster than
  Wasp only on `terrain30-c-z`. On roads this needs the round-2 band and leaf
  pruning. On `rmat25` equal threads cost GAPBS 1.4×; at its preferred 64
  threads ACIC-SSSP is 1.08–1.15× slower than it. Absolute one-node times
  vary up to 1.8× between Delta nodes (cn099 against cn022). The distributed
  claims do not depend on it; any "competitive everywhere" sentence does.
- **Roads at scale.** Roads beyond 16 nodes; Wasp still wins on `road-usa-z`
  at every node count.
- **Scale-free graphs.** RIKEN is 2.4–3.1× faster on RMAT 25–27. "Regardless
  of input structure" in the abstract is sensitive to this.
- **Vertex order.** A locality-preserving order (Morton) is assumed and given
  to every code. Row-major `mesh26` is 107–183× slower with tiling on, and
  Gluon wins there. The locality rule turns tiling off there and recovers
  21–92× (F15b, §40), but untiled ACIC on row-major input is not the headline
  configuration. The mesh and road results are sensitive to this assumption.
- **Bucket width.** One rule (ln V / 8 below degree 8, ln V otherwise, 131072
  on roads), within 1.5× of the best width tried on every input;
  `mesh28-w10-z` at 64 nodes is the largest gap and plateaus.
- **Weights.** Mesh and grid weights are uniform synthetic; only roads and
  terrain are natural. Roads use distances, not travel times.
- **Baseline tuning.** Settings chosen at a grid boundary limit any
  best-possible claim for that baseline; say which ones.
- **Timing boundary.** Solve time only; graph loading and ingest are reported
  separately.
- **Scope.** CPU only (no GPU comparison; see §7's notes); exact
  single-source SSSP; static graphs.

### Venue facts ([CFP](https://www.ipdps.org/ipdps2027/2027-call-for-papers.html))

**Dates:**
| Item | Date |
|---|---|
| 500-word abstract | Oct 1, 2026 AOE |
| Paper | Oct 8 AOE (firm) |
| Early rejects | Nov 30 |
| Rebuttal | Nov 30–Dec 3 |
| First-round decisions (accept, revise, reject) | Dec 18 |
| Revision with cover letter | Jan 18, 2027 |
| Final decisions | Feb 2 |

**Rules:**
- Ten pages, figures included; references unlimited.
- No appendix at submission; a reproducibility appendix is required after acceptance.
- Double-anonymous.
- Review criteria named in the CFP: motivation, the difference from prior work,
  key insights, methodology, and **limitations of the approach**.
- Inaccurate references are grounds for rejection: verify every citation
  against the published paper.
- Any AI-generated text is disclosed in the acknowledgements.

**Tracks:** primary **Algorithms**, secondary **Programming Models, Compilers,
and Runtime Systems**.
- Algorithms names graph algorithms explicitly, and its committee holds most of
  the conference's distributed-graph and SSSP reviewers.
- The runtime track fits the co-scheduling evidence (heap slices restore
  message interleaving; scheduler order, #258). As primary, though, it would
  demand a general runtime contribution, which the paper does not make.
- Applications expects innovations that come from a specific application
  domain. Measurements would recast a new algorithm as a benchmarking study.

### Expected reviewers ([PC](https://www.ipdps.org/ipdps2027/2027-program-committee.html))

Assignments are not public. The areas below are recalled from the members'
publications; verify each citation before using it.

| Member (track) | Likely check | Response |
|---|---|---|
| Roger Pearce (Algorithms), HavoqGT's author | HavoqGT run fairly: layout, delegates, store, ingest | Record exact settings; report ingest separately and outside the solve time; disclose `benchmarks/havoqgt.patch` (shutdown path and zero-delegate allocation only); run it on RMAT too (O2), with the upstream delegate threshold and one set to the rank count. |
| Fabio Checconi (Algorithms), co-author of Chakaravarthy et al., IPDPS 2014 (distributed SSSP at scale on RMAT) | Graph500/RMAT framing | Cite and discuss that paper. State "not covered by Graph500" precisely: its SSSP kernel runs only on Kronecker graphs. |
| George Slota, Ariful Azad, Kathrin Hanauer (Algorithms) | Partitioning and vertex order; experimental rigor | The ordering pair (O1). At least four held-out sources per ACIC point in the final matrix, with spread shown. |
| Grey Ballard (co-chair), Azad, Sayan Ghosh (Algorithms) | Why ACIC wins: work and communication, not only time | Work-efficiency figure (edges relaxed per graph edge) and wire bytes per edge, from counters in the final build (D2). |
| Sanjukta Bhowmick (co-chair); Arindam Khanda (Applications) | Dynamic SSSP updates | One line of related work; out of scope. |
| Joseph Schuchart, Jonas Posner, Claudia Fohry, Hans Vandierendonck (Runtime) | Which runtime lesson carries beyond SSSP | One paragraph and one figure on runtime/application co-scheduling. |
| Jesun Firoz (Measurements) | Probably not assigned; his runtime scheduling policies for distributed asynchronous SSSP (IPDPS 2018) and the Distributed Control line (Kanewala, Zalewski, Lumsdaine) are the closest prior work | Cite and set apart from ACIC, with Madduri et al., DIMACS 2006 (delta-stepping at scale). |

**Conflicts:** no Urbana faculty are on the committee. Nikhil Jain (ML track)
is a PPL alumnus; declare a conflict only for recent co-authorship.

**Framing for the Algorithms track:** present ACIC as an algorithm, with
pseudocode and a correctness and termination argument (monotone frontier,
collective emptiness). Its parts are process-shared priority queues with
nearest-bucket batched removal, and the chunk queue on meshes and terrain
(F4 holds there; §35). Avoid presenting it as runtime tuning.

The adaptivity claim rests on the flush policy and the tiling rule (Pillar 2),
not on the admission threshold. The threshold is inert on one Delta node (D4)
and at 16 and 64 Frontier nodes (F10: top bucket in 89–99% of rounds), and
1.08–1.67× slower when engaged (D4b, F10). The paper describes it as a safety
bound. Heap slices are worth nothing on F10's large inputs; the paper claims
them only for the strong-scaling limit, where §8 measured them (`mesh26-z`,
older code), or not at all. At scale the parts that carry the speedup are
tiled placement (F13), starvation-gated flushing (F11), and, within a process,
nearest-bucket batched removal and the chunk queue on meshes and terrain.

**Vertex order (O1, 2026-09-29):**
- The mesh and road results assume a locality-preserving order (Morton here).
- Row-major `mesh26` is 107–183× slower for ACIC at 16 nodes, and Gluon wins
  there (current-state §33).
- The paper states the assumption and gives every code the same order.
- O1b tests whether reader tiling causes most of the loss. If it does, the
  fix (tiling only for locality-preserving inputs) is a rule change for the
  freeze.

**Early rejects:** the abstract and page 1 carry the regime and the headline
speedups.

### Experiments by machine

One-node experimentation runs on **Delta** and scaling on **Frontier**
(user, 2026-09-28). Figures that set one-node points against Frontier
scaling use one-node runs on Frontier (the debug queue). Delta binaries do
not run on Frontier: the frozen source revision is rebuilt there.

#### Delta, one node, round 2 (proposed 2026-09-30; nothing submitted)

Goal: a same-code, same-node one-node comparison for the paper, then cheaper
one-node execution. The Delta Wasp comparisons so far (§§16–23) ran on
87f04af or the pre-commit chunk tree, not the freeze. Delta has no same-node
GAPBS, and the 3-D grid and terrain have no Delta Wasp at all.

**Phase A: measure the freeze (1–2 days, no code change).**

| # | Experiment | Why |
|---|---|---|
| D7 | Freeze binaries at the paper's per-family flags (heap slice 8; band-256 chunks slice 64 on the mesh, grid and terrain; band-65536 chunks slice 64 on roads; width ln V / 8 where degree < 8), against **Wasp and GAPBS, both tuned on the same node** (Δ on two training sources at ACIC's 112 PEs as threads, then frozen; revised 2026-10-02 from threads × Δ). Inputs: `mesh26-z`, `mesh28-z`, `mesh30-z`, `grid3-30-z`, `terrain30-c-z`, `road-usa-z`, `road-eu-z`, `rmat25`. Four held-out sources, one warmup, three repetitions, `onenode_ab` plus the audit | Replaces §17/§20 with freeze-code numbers on every family, and gives Delta's first GAPBS column. The abstract's one-node sentence would then rest on the same binaries as every Frontier number |
| D8 | Layout screen on the freeze: 16 × 7 (current), 8 × 15, 32 × 3, 128 × 1 (`LCI_ATTR_NPACKETS` scaled as in F14b) on `mesh28-z`, `grid3-30-z` and `road-eu-z` | F14 found one queue per L3 region best on Frontier and per-core processes comparable; Delta's 16-core NUMA domains may prefer another split |

**Phase B: one-node improvements, in order of expected gain per effort.**
Each is an opt-in flag, measured on Delta against the freeze with
predictions, and adopted only through the gate below.

| # | Change | Evidence | Expected |
|---|---|---|---|
| O1 | **Bucket-array bins.** Replace each bin's `std::map<long, priority_queue>` with a circular array of buckets indexed by bucket number (vectors, or the chunk queue's 64-item blocks), as Wasp's buckets are | Heap push 238 ns and pop about 650 ns, 52% of PE time on `mesh28-z` (§16); the chunk queue cut queue CPU from 57% to 23% (§25) | 1.2–1.5× over the chunk queue on the mesh, grid and terrain; also removes the chunk queue's band as a per-family choice |
| O2 | **Drain from the idle hook** instead of self-sent `process_heap` messages | Self-callback latency p50/p90 0.48/1.61 ms, up to 506 pending per PE (§16); the earlier coalescing prototype failed the gate (§15), so this is a different mechanism, not a retry | Largest on roads, which have 1.5K–2.4K rounds (§23) |
| O3 | **Degree-1 pruning** (Wasp's leaf pruning). A degree-1 vertex's only edge leads back to its parent, so set its distance without enqueuing it | Wasp does it; the fraction of degree-1 vertices is measured first with a `graph_digest`-style count | Roads only; 1.1–1.3× if 10–25% of road vertices are leaves |
| O4 | **One-node controller cadence.** Screen `reduction_delay` (the round interval) on one node | Roads are round-bound: fewer rounds did not help at 8–14% (§3 claim), so this is a check, not a bet | 0.9–1.2×; stop if flat |

**Gate for adopting any change into the paper configuration:**
1. Every digest matches, on Delta one node and on the D7 inputs.
2. No regression on Frontier: one 16-node and one 64-node job on `mesh32-z`,
   `terrain30-s-z` and `road-planet-z` against the freeze, within 0.95×.
3. Ready by 2026-10-05. Otherwise the paper uses the freeze, and the change
   goes to the final version or the follow-on ACIC paper.

**Order:** D7 first, since it alone fixes the one-node claim. Then O1 (the
largest measured cost) and O3's leaf count (an hour). O2 and O4 only if time
remains. D8 can share D7's allocation.

**Round 2 execution (started 2026-09-30; O items first, at the author's
request).** Code 986c0d8 (all three changes opt-in, off by default); every
binary in `acic-ipdps27-delta-20260928/bin` with an `o` label is built from
it. `cpuintqos` now allows one submitted job, so jobs run one at a time.

| # | Job | Status |
|---|---|---|
| O3 leaf count | login node | road-usa-z 19.9% leaves, road-eu-z 25.8%; meshes and terrain none |
| O4 | 22575593 | **Stop.** Every `--round-delay` (0.02–0.5 ms) is 0.88–1.00× on road-usa-z and 0.98–1.01× on road-eu-z, with 1.3–2.8× fewer rounds (current-state §39) |
| O3 | 22575807 | **Works.** `--leaf-prune on` is 1.10–1.14× (road-usa-z) and 1.10–1.17× (road-eu-z) over off on the band-65536 chunk profile, 1.01–1.10× and 1.11–1.22× on the heap |
| O1 road-eu-z | 22576060 | **Band, not array.** Bucket queue at band 1024, width 16384: 7.2–8.4× over the paper road profile, 2.1× over the freeze heap at width 16384. The paper road profile runs 19–22 edge attempts per edge on road-eu-z |
| Two-node gate, O flags | 22576273 | **Passed**, 175/175 runs over seven profiles, including an undirected leafy graph from a degree-1 source |
| O1 mesh28-z, terrain | 22576508, 22577056 | **Array flat:** 0.96–1.03× and 0.96–0.99× of the freeze chunk queue at band 256 |
| O2 mesh28-z, roads | 22576591, 22576660 | **Reject:** 1.10–1.21× slower on the mesh (rounds 28–36× more often), mixed on roads |
| O1 road bands | 22576845, 22577011, 22576916 | Best band 8192–16384 on road-usa-z, 256–1024 on road-eu-z, at the paper width 131072 |
| Road confirmation | 22577196, 22581424 | Bucket queue at the per-road band with leaf pruning, over the paper road profile: **1.38–1.64×** (road-usa-z, band 16384) and **7.22–9.84×** (road-eu-z, band 1024) |
| O1 grid3-30-z | 22581589 | **Array flat** (band 256 0.97–1.00×); band 1024 is 1.7× slower, so no weight-proportional band rule |
| D7 | **Done** 2026-10-02 (current-state §41) | **Comparisons use equal PEs/threads (user, 2026-10-02): GAPBS and Wasp run at 112 threads, ACIC's 16 × 7 = 112 workers, packed (`srun -c 112`, OMP close), and only Δ is tuned** (steps of 2, two training sources, `--tune-only`); `d7_summary.py` rejects any comparison whose baseline threads differ from ACIC's workers. Then `d7_compare.sbatch` (ACIC arms, then both baselines at their frozen selections, same allocation, four held-out sources). ACIC arms: freeze chunks and heap at width ln V / 8 (meshes, grid, terrain); the paper road profile, the heap and the round-2 candidate (per-road band + leaf pruning) on roads; the heap on `rmat25`. Superseded: every earlier D7 comparison (22582308, 22582508, 22582937, 22583102, 22583270, 22583538, 22583809, 22623748), whose baselines ran at their own best thread count (64–128); the spread runs also ran with whole-node OMP spread, measured 1.06–1.4× slower than packed (22621300, 22623748). All eight inputs complete at 112 PEs and threads, every solve valid. Best ACIC ties or beats GAPBS on six of eight; Wasp is faster on seven of eight (not on `terrain30-c-z`). `grid3-30-z` needed a second tune (22627665, Δ 128–2048). The cn099 check (22625443) shows 1.4–1.8× node-to-node variation in baseline times, which explains the §23 road-eu-z Wasp gap. `gap_sssp` rebuilt with GCC 14; `wasp_sssp` is bit-identical to §§16–23's |
| Adoption | — | Candidates: O3 (`--leaf-prune`) and a per-road band (the bucket queue's `--chunk-band`). Gate item 2 (Frontier 16/64 nodes, including `road-planet-z` at a narrower band) is the author's |

#### Delta, one node, can start now

| # | Experiment | Why | Needed by |
|---|---|---|---|
| D1 | **Done.** One-node flag sets: band-256 chunks with slice 64 for mesh (2.38–2.56×), 3-D grid (1.79–1.81×) and terrain (2.31–2.35×); band-65536 chunks with slice 64 for road (1.06–1.30×). Each band loses on the other family (current-state §27) | 22526239, 22527563, 22528376, 22529053 | **done** |
| D2 | Counter build: edges relaxed per graph edge and wire bytes per edge, printed on every solve. Overhead ≤ 2% against the uncounted binary on `mesh28-z` and `road-usa-z`. | Ballard, Azad and Ghosh will ask why ACIC wins (work efficiency and traffic figure). | Oct 2 (freeze) |
| D3 | `mesh28-w10-z`, heap slice 8. **The ln V rule is the slowest width from ⅛× to 16×, and round-bound** (24.8K rounds). Wide: 16× 1.08–1.20× with flat work. Narrow: ⅛× 1.29–1.38× with 30–50% more work. Nothing ruled out. **F5 nominations: 16× (310.5), then ⅛× (2.43)** (current-state §27) | 22528693, 22533655 | **done** |
| D4 | One mechanism off at a time, plus chunks. **mesh28-z (22528885), relative to the candidate's time:** sharing off 2.85–4.12×, local queue 1.88–2.37×, batch 1 1.44–1.47×, no slice 0.89–0.93×, plain async 0.98–1.00×, chunks 0.38–0.43×. **The histogram threshold is inert at the ln V width** (1,145 of 1,148 rounds outside the window); this needs a decision on the paper's framing, and F10 must show whether it holds at scale (current-state §28). **D4b (22529681): engaged by the weight-rule width, the threshold cuts work 10–28% but costs about 20× the rounds, and is 1.33–1.41× slower** (current-state §29) **Terrain (22529683):** the same ordering (sharing off 1.74–2.93×, local queue 1.57–1.89×, batch 1 1.33–1.37×, no slice 0.88–0.89×, plain async 0.99–1.01×, chunks 0.42–0.43×); the threshold is inert (99.9% of rounds at 2047) | 22528885, 22529681, 22529683 | **done** |
| D5 | `mesh26` against `mesh26-z`: Morton order is 2.96–4.21× faster on the heap and 4.12–7.11× with band 256. Generator order has 4–8× the edge work and about 40× the inter-process bytes, so on one node the effect is mostly work and traffic, not cache locality (current-state §30) | 22530871 | **done** |
| D4c | Follow-up to D4b, run at the user's request (2026-09-29): work-cost builds on mesh28-z. The engaged threshold's slowdown is about half PE starvation (idle share 0.08–0.12 → 0.17–0.24) and about half shared-queue contention (half the items per pop, 1.4–4.1× the cost per pop). The reduction and broadcast themselves are minor (current-state §31) | 22543996 | **done** |
| D6 | Freeze candidate 601697e (D2 counters, `--admission`, defaults unchanged); Delta binaries `acic_frz_{heap,c256,c65536}`, used by D1, D3, D4 and D5. Flags per family from D1: build `-DACIC_PROCESS_CHUNKS -DACIC_CHUNK_DISTANCE_WIDTH=256` for mesh, grid and terrain and `=65536` for road, with `--heap-slice 64`; F4 tests whether the chunk queue holds at 16 and 64 nodes before the frozen matrix uses it **Two-node envelope gate passed** (22544422): 7 profiles × 21 runs = 147 of 147. The profiles cover the production heap (default, candidate, `--admission all`), band-256 and band-65536 chunks, and `WIRE=compact64` heap and band 256, on generated graphs and on `.wsg` inputs with reader tiling engaged (`scripts/delta/freeze_gate_2node.sbatch`) | 22544422 | **validated on Delta; ready for F-freeze** |

**Delta inputs:**
- `grid3-30-z` is regenerated with the existing generator or transferred (about 50 GB).
- `terrain30-s-z` (16.2B edges) needed a 512 GB Frontier node, so it will
  likely not fit a Delta node. Cut a GLO-30 crop of about 0.5B vertices
  instead, using the terrain pipeline's `REGION`.

#### Delta execution (started 2026-09-28)

Campaign `/work/hdd/mzu/rao1/acic-ipdps27-delta-20260928`. Jobs use
`--account=rfp-delta-cpu` (priority 3377 against 1322 on mzu, 2026-09-28) and
`cpu-interactive` where they fit an hour. That QOS allows two queued jobs and
one running job per user, so timing jobs run one at a time and input builds go
to `cpu`. Driver: `scripts/delta/ipdps_ab.sbatch` (`onenode_ab.py --batch`,
16 × 7, four held-out sources per launch, rotated arms, every solve
digest-checked). Predictions are recorded in `benchmarks/delta-ipdps-*-variants.json`.

| Item | What was built | Jobs | State |
|---|---|---|---|
| Inputs | `grid3-30-z`, `mesh28-w10-z` (`scripts/delta/ipdps_prepare.sbatch`, `grid_graph`; 1:02 and 0:17 to generate; references 21:15 and 2:05, 110 GB peak); `terrain30-c-z` = GLO-30 lat [3, 9) × lon [24, 31), seed 6N 27E, 42 tiles, nested in `terrain30-s-z` (`scripts/delta/fetch_terrain_crop.sh`; rasterio's GDAL 3.9.3, since Delta has no GDAL module); `mesh28-z`, `road-usa-z`, `mesh26`, `mesh26-z` linked from earlier Delta campaigns | 22526088, 22527562 (done); terrain 22528231 (544,320,000 vertices, 4.35B arcs; reference 11:54) | done |
| D2 | `WORK_EFFICIENCY` on every solve (34137dd). Overhead against 929a830: geometric mean 1.002–1.014 on the mesh and road profiles | 22526239, 22527563 | **done, kept** (current-state §26) |
| D1 | Mesh and road re-confirmed: band 256 on the mesh 2.38–2.56×, band 65536 on road 1.06–1.30×, each losing on the other family. **grid3-30-z: band 256 1.79–1.81×, adopt** (current-state §27). Terrain: heap / band 256 / heap control (band 65536 dropped: mesh-scale weights) | 22526239, 22527563, 22528376; terrain 22529053 | mesh, road, grid done; terrain queued |
| D3 | `mesh28-w10-z`, heap slice 8, widths ¼×–4× ln V (19.41) | feeder | queued next |
| D4 | One mechanism off at a time (slice, batch, nearest, sharing, histogram admission) plus chunks; `--admission all` (601697e) is the plain asynchronous arm, also needed by F10 | feeder | variants committed |
| D5 | `mesh26` against `mesh26-z`, heap and band 256 | feeder | variants committed |
| D6 | Freeze candidate 601697e (D2 counters, `--admission`, defaults unchanged); Delta binaries `acic_frz_{heap,c256,c65536}`. D1, D3, D4 and D5 run on it | — | candidate built; flags per family await D1 |

#### Frontier, scaling (4/16/32/64 nodes, never more than 64)

| # | Experiment | Status |
|---|---|---|
| F1 | Phase A: Gemini and HavoqGT on the series, 4/16/32/64 nodes | **Done** (5560447–5560462): ACIC is faster everywhere, 17–24× over HavoqGT on terrain and ≥ 245× over Gemini on the 2-D meshes (current-state §33) |
| O1 | Ordering pair: `mesh26` and `road-usa` in generator/DIMACS order against their Morton (`-z`) forms, ACIC and Gluon, 16 nodes, four held-out sources (the same physical sources) | **Done** (5561485, 5561486). Order cost to ACIC: 107–183× on `mesh26`, where Gluon wins (ACIC 0.23–0.36×); 20–28× on `road-usa`, where ACIC is still 38–61× faster (§33) |
| O1b | Reader tiling off against auto on `mesh26`, `mesh26-z`, `road-usa` and `road-usa-z`, 16 nodes, `acic_scale64b` as O1 | **Done** (5565472): tiling causes the `mesh26` loss (off: 61–131× faster, order cost about 1.7×), but tiling wins 1.7–4.4× everywhere else. A locality-based tiling rule is a candidate change (current-state §34) |
| O2 | HavoqGT (delegate threshold 2^20 and 896) and Gemini on `rmat25` and `rmat26`, 16 nodes | **Done** (5561482–5561484). ACIC 1.35–2.59× over Gemini and 4.3–12× over HavoqGT. The upstream threshold makes no delegates; with 896, every solve hung (§33) |
| O2b | HavoqGT delegate probe: `rmat20`, 2 nodes, thresholds 2^20/65536/4096/896, gdb stacks on a hang | **Done** (5565473): all correct; 6,196 delegates make it 2.4× faster (0.71 s against 1.68 s) |
| O2c | The same thresholds (65536/16384/4096/896) on `rmat25` at 16 nodes, where 896 hung | **Done** (5566201): 4096 (15,276 delegates) is fastest at 1.38 s; 896 deadlocks in HavoqGT's collectives |
| O2d | HavoqGT at threshold 4096 on `rmat25`/`rmat26`, 16 nodes, four held-out sources: the RMAT row the paper uses | **Done** (5566503): `rmat25` 1.21–1.46 s; `rmat26` (83,682 delegates) hung. O2e (5566974): `rmat26` at 16384 (9,109 delegates) correct but no faster (1.91–2.26 s). The paper uses HavoqGT's best completing setting: ACIC 5.95–9.32× (`rmat25`), 4.29–7.78× (`rmat26`) |
| F-freeze | Freeze candidate 601697e built on Frontier: `acic_frz_heap`, `acic_frz_c256`, `acic_frz_c65536` (TLS runtime, compact64; manifests beside the binaries). Every F run is digest-checked, so no separate smoke | **Done** 2026-09-29 |
| F4 | Chunk queue at 16 and 64 nodes: band 256/slice 64 against the heap/slice 8 on `mesh28-z`, `mesh32-z`, `grid3-33-z`, `terrain30-m-z`; band 65536 on `road-planet-z` | 16n **done** (5565464): chunks 1.65–1.73× (`mesh28-z`), 2.12–2.15× (`mesh32-z`), 1.85–1.93× (`terrain30-m-z`). The chunk arm failed on `grid3-33-z` (no progress by the step limit) and `road-planet-z` (one source did not converge in 300 s). Stall probes: C1 5566199 found road chunks correct but 6–100× slower or timing out; C2 5566200 found `grid3-30-z` chunks correct and 1.9× faster. 64n **done** (5565465): `mesh32-z` 1.91–1.98×, `terrain30-m-z` 1.72–1.80×, `mesh28-z` only 1.04–1.14×; `grid3-33-z` 1.97–1.98× on three sources, but the fourth stalled (2.35–134 s with the same rounds and work); road chunks hit the step limit. The chunk queue has a progress defect at scale (correct, same work, long waits). Proposed for F8: chunks for mesh and terrain, the heap for roads and 3-D grids unless fixed (current-state §34, §35) |
| F5 | `mesh28-w10-z` widths at 32 and 64 nodes: the ln V rule, 16× (310.5) and ⅛× (2.43), the D3b nominations. Run directly: each solve takes under a second | **Done.** 32n (5565466): 16× 1.75–1.84×, ⅛× 1.53–1.57×; 64n (5565465): 16× **1.96–2.21×**, ⅛× 1.55–1.68× faster than the rule, and 16× scales again from 32 to 64 (about 1.2×). The ln V width is round-bound on this input at scale. Changing the frozen rule is a decision for F8 (§35) |
| F6 | RIKEN, pinned at 8 ranks per node and delta ≈ 2× the mean edge weight (the ratio its mesh searches chose). Only narrow inputs (the driver reads at most 2^31 − 1 vertices) whose distances stay below 2^24: `terrain30-s-z` (d 2048/65536), `grid3-30-z` (1024/1024), `mesh28-w10-z` (16/16). `mesh32-z`, `grid3-33-z` and the larger terrain crops are wide; `mesh28-w64k-z` exceeds 2^24 | **Done** (5565469, 5565470), exact digests. ACIC's speedup: `grid3-30-z` 31.6–55.5× (16n) and 28.0–43.3× (64n); `mesh28-w10-z` 202–334× and 81.2–121×; `terrain30-s-z` 235–250× at 64n. At 16n RIKEN aborts on `terrain30-s-z` (32-bit size overflow at 128 ranks) |
| F7 | Gluon delta check on `terrain30-s-z` at 64 nodes: deltas 32, 512 and 2048 on one source, against the series' 128 (499–509 s) | **Done** (5565471): 32/512/2048 took 577–636 s against 128's 499–509 s; Gluon was not mistuned |
| F8 | Final ACIC matrix with the freeze binaries, flags per family chosen by F4: terrain, meshes, grids, roads (including 32 nodes) at 4/16/32/64, and scale-free at 16/64; four or more sources, D2 counters on every solve | **Done** (§36): 1,560 digest-valid solves; heap within 4% of the series; chunks 1.5–2.3× on large meshes, `grid3-30-z` and terrain, 0.38–0.87× on `mesh28-w64k-z`, so the headline uses the heap. Submitted 2026-09-29 (user: "submit all these experiments"). Freeze binaries unchanged: tiling auto and the ln V width kept (O1b, F5 as limitations until the width check decides). Frozen = heap/slice 8 on every family, plus a chunk arm (band 256, slice 64) on meshes, terrain and `grid3-30-z`; `grid3-33-z` and roads heap only; scale-free 4 × 14 (orkut 8 × 7). Mesh 4/16/32/64n 5568629/5568631/5568633/5568635; road 5568630/5568632/5568634/5568636; `terrain30-l-z` 32/64n 5568637/5568638; scale-free 16/64n 5568639/5568641; one node (debug, chained): mesh, road, then `mesh30-z`/`terrain30-s-z` (`frontier-f8-*-variants.json`) |
| W | Width check for the F8 width decision: the ln V rule, ⅛×, 4× and 16× on `mesh28-z`, `mesh28-w10-z`, `mesh28-w64k-z`, `grid3-30-z`, `mesh32-z`, `terrain30-s-z` | **Done** (5568640, 5568642): ⅛× (ln V / 8) wins or ties everywhere (1.46–1.69× `mesh28-w10-z`, 1.06–1.18× `grid3-30-z`); 4× and 16× lose up to 1.9× on `grid3-30-z`. Adopting ln V / 8 is the user's decision; roads and scale-free untested (§36) |
| F8w | **Width ln V / 8 adopted** (user, 2026-09-29): F8 rerun on every rule-width input (meshes, grids, terrain) at the same node counts, heap and chunk arms, `--bucket-width` = the F8 rule width / 8 on the unchanged freeze binaries. Scale-free at 16/64 with the rule kept as a control arm (⅛× untested there). Roads keep 131072 | **Done** (§37): ln V / 8 is 1.47–1.66× on `mesh28-w10-z` at 16–64 nodes and 1.06–1.17× on `grid3-30-z`, neutral elsewhere; chunks lose on `mesh28-w10-z`; scale-free keeps the rule (the narrow width is 5–11% slower on RMAT at 64 nodes) |
| F11 | Aggregation ablation at 16 and 64 nodes (freeze heap, new width): defaults against `--flush-policy fixed`, `--idle-flush off`, `--idle-flush on`, `--idle-flush-interval 0`, `--bufsize 64`, and the naive-distribution arm (`--flush-policy fixed --idle-flush off --reader-tile off`); `mesh32-z`, `terrain30-s-z`, `road-planet-z`, `rmat26`; three sources, two repetitions | **Done** (§37): the starvation-gated flush is 1.2–1.7× over a fixed cadence on the mesh, terrain and roads (it halves attempts per edge at 64 nodes); the idle-flush settings are inert there (only `rmat26` at 64 nodes pays 1.74× without them); naive distribution is 4.8–6.9× slower at 64 nodes on the mesh and terrain, so the stop rule is not triggered |
| F10b | F10 `terrain30-m-z` at 64 nodes again, 15-minute step limit | **Done** (5568643): complete; no sharing 9.1–14×, threshold inert (96.8–99.2% top bucket), engaged 1.79–1.89× slower (§36) |
| F6b | RIKEN `terrain30-s-z` at 16 nodes with 14 and 28 ranks per node (the 8-rank layout overflows) | **Done** at 56 ranks per node (5569517, §37): RIKEN takes 1,212–1,229 s at 16 nodes; ACIC is 200× faster |
| F13 | Tiling off alone at 64 nodes (the freeze heap, width ln V / 8): candidate, `--reader-tile off`, fixed flush, and F11's naive arm; `mesh32-z`, `terrain30-s-z`, `road-planet-z` | **Done** (§38): tiling off alone costs 6.9–7.4× on `mesh32-z`, 5.0–6.8× on terrain and 1.4–1.8× on roads at 64 nodes, as much as the naive arm; the flush and tiling effects do not multiply |
| F14 | Priority-domain sweep: 56 workers per node as 8 × 7 (candidate), 4 × 14, 2 × 28 and 1 × 56, sharing on; `mesh32-z`, `terrain30-s-z`, `grid3-30-z`, `road-planet-z` at 16 and 64 nodes | **Done** (§38): larger shared domains are slower: 4 × 14 1.1–1.4×, 2 × 28 1.2–1.8×, 1 × 56 1.7–2.9×, though they waste less work on the mesh, terrain and grid |
| F14b | The per-core end: 8 × 7, 8 × 7 with `--process-share off`, and 56 × 1 (never run past one node, so in its own jobs); same inputs | **Done at 16 nodes; 64 nodes timed out** (5570584: grid and roads complete, terrain one repetition, `mesh32-z` not run) (§38): 56 × 1 is 0.72–1.50× of 8 × 7's time; `--process-share off` is a different code path (threshold-deferred TRAM inside a process), so the 5–14× 'shared queues' figure is withdrawn. First submission 5570492/5570495/5570496 aborted at LCI start-up (packet pool registration) |
| F15 | Locality-aware tiling (`acic_frz_tloc`, 30737e4 = freeze plus `--reader-tile locality`): auto, locality, off (and target 0.8 on `mesh26`) on row-major `mesh26`, `mesh26-z`, DIMACS `road-usa`, `road-usa-z` at 16 and 64 nodes | **Done** (5570551, 5570552; §38): `--reader-tile locality` is 26–29× faster than auto on row-major `mesh26` and neutral on the Morton inputs, but its 16-row tiles at 16 nodes are 2.9× behind tiling off, and on DIMACS `road-usa` at 64 nodes it is 1.96× slower than auto. Proposed rule revision: the auto tile or off, nothing in between (not built) |
| F14c | F14b's missing 64-node cells: 8 × 7 against 56 × 1 (with the LCI pool fix) on `mesh32-z` and `terrain30-s-z` | **No 56 × 1 data** (5573561, §40): both 56 × 1 warmups hit the 12-minute step limit, shorter than one 56 × 1 launch at 64 nodes (13–14.5 min in F14b, mostly start-up and reading); the harness reran 8 × 7 alone. A rerun needs about a 20-minute step limit and one job per input. Not resubmitted |
| F15b | The revised locality rule (`acic_frz_tloc2`, c3dc360: the auto tile or off) against auto and off on row-major `mesh26` and DIMACS `road-usa` at 16 and 64 nodes | **Done** (5573562, 5573563; §40): 108 digest-valid solves; the rule chose off on `mesh26` and auto's tile on `road-usa` at both node counts, as the tool predicted. Locality is 21–92× faster than auto on `mesh26` and as fast as off (0.99–1.11×); on `road-usa` 0.95–1.16× of auto and 2.2–2.9× faster than off. Both F15 misses fixed |
| F8-8n | The 8-node point of the Fig. 4 scaling series (user, 2026-10-03): the F8w configuration at 8 nodes on the nine F8w inputs run at 4 nodes, `terrain30-m-z` and `grid3-33-z` (first run below 16 nodes; step limit 15 min), and the F8 roads. Variant files are the 4n (16n for the two large inputs) files with nodes 8; predictions: every 8n median between the 16n and 4n medians (1.3–2.2× the 16n median for the two large inputs) | Submitted 2026-10-03: 5592439 (F8w inputs, 2 h), 5592440 (`terrain30-m-z`, `grid3-33-z`, 1.5 h), 5592441 (roads, 1 h) (`frontier-f8w-*-8n-variants.json`, `frontier-f8-road-8n-variants.json`) |
| O1c | Native order for the other baselines at 16 nodes: RIKEN on `mesh26`/`mesh26-z`; Gemini and HavoqGT on `mesh26`, `mesh26-z`, `road-usa`, `road-usa-z` | **Done** (5568646, 5568648, 5568649). Gemini prefers row-major on `mesh26` (order cost 0.62–0.84×); RIKEN and HavoqGT prefer Morton. At each code's best order ACIC is 37–43× (Gluon), 251–256× (RIKEN), 362–416× (Gemini), 213–233× (HavoqGT) faster on `mesh26`; 32–342× on `road-usa` (§36) |
| F9 | One-node Frontier points: GAPBS then Wasp on `road-planet-z`, chained after O2b; the debug queue takes one job at a time. ACIC at one node goes with F8 | GAPBS **done** 1.64–1.92 s (5565476), Wasp **done** 0.86–0.92 s (5565797); ACIC with F8 |
| F10 | Ablation at scale, the Delta D4/D4b arm set: candidate, no slice, batch 1, local queue, no sharing, plain async (`--admission all`), chunks, and the threshold engaged by `--bucket-width-rule weight`, with and without admission. `mesh32-z` and `terrain30-s-z` at 16 nodes, `mesh32-z` and `terrain30-m-z` at 64; three sources, one warmup, two repetitions. **It checks whether the histogram threshold acts at scale** (round counts in every log) | **Done** (5565467, 5565468; the 64n `terrain30-m-z` cell has warmup solves only). Candidate over the arm: no sharing 4.8–15×, local queue 2.2–3.7×, batch 1 1.5–1.6×, no slice 0.91–1.00×, plain async 0.97–1.00×, chunks about 0.5×. **The threshold is inert at scale too** (top bucket in 89–99% of rounds); engaged, it is 1.08–1.67× slower (§35) |

The F batch holds about 380 node-hours at its time limits. Predictions are in
`benchmarks/frontier-{f4,f10,o1b}-*-variants.json`, recorded before
submission.

**Dropped:**
- Phase B (lower bounds on the large inputs).
- Further `terrain-ae-z` runs: the GLO-30 series supersedes them, and its 8- and 16-node rows stay supplementary.
- New input families.

**Limitations section:** see "Paper layout" above (§8 and its list).

#### Writing and anonymity (no machine)

- The abstract and track registration go in by Oct 1 AOE.
- Pick a title distinct from the IA³@SC24 paper. Cite that paper in the third
  person with an explicit statement of what is new. If the overlap with a
  workshop paper is in doubt, ask pc2027@ipdps.org.
- Make the public repository private or anonymized before Oct 8; it holds
  `design/acic_2024paper.pdf`.
- Related work: see "Paper layout", §7 and its table. Verify every entry.

### Timeline

| Dates | Delta | Frontier | Writing |
|---|---|---|---|
| Sep 28 | D1–D5 done | F1, O1, O2 done | — |
| Sep 29–30 | — | F-freeze done; F4–F7, F9, F10, O1b, O2b–O2e done 2026-09-29; F8 waits on three decisions (chunks, tiling rule, width) | Abstract draft by Sep 30 |
| Oct 1 AOE | — | F4/O1b decide F8's flags | Abstract and tracks registered |
| Oct 2 | — | F8 submitted as one batch, ACIC one-node F9 points with it | Methods, algorithm |
| Oct 3–5 | — | F8 running | Evaluation from arriving data |
| Oct 5–8 | — | Reruns of failed cells only | Full draft, audit, submit |

### Reviewer-driven jobs submitted 2026-09-28

Predictions are recorded in `benchmarks/frontier-order-{mesh,road}-16n-variants.json`.
All jobs have a 1–1.5 h limit.
- **O1, ACIC** (`gluon_compare.sbatch`, `SKIP_GLUON=1`,
  `VARIANTS_PREFIX=frontier-order`), job 5561485. One arm, frozen =
  `acic_scale64b`. Mesh flags as the series; road adds `--bucket-width 131072`.
  Four test sources, one warmup and three repetitions.
- **O1, Gluon** (`series_gluon.sbatch`), job 5561486. Gluon-Async, 8 ranks,
  oec. Delta 64 on meshes and 524288 on roads, as its latest searches selected
  on the `-z` inputs at 16 nodes (job 5541370; road selections vary between
  jobs: 32768 to 8388608). Four sources, 600 s cap per launch.
- **O2** (`series_baseline.sbatch`):
  - `rmat25` and `rmat26` converted to `.gemini32` in job 5561481.
  - Gemini: job 5561482, one rank per node × 56 threads.
  - HavoqGT: 56 ranks per node, ingested into `/dev/shm`. Job 5561483 uses the
    upstream delegate threshold (2^20); job 5561484 uses
    `HAVOQGT_DELEGATE=896`, the rank count.
  - Four sources and a 600 s cap per launch.

## Paper decision

The [IPDPS 2027 CFP](https://www.ipdps.org/ipdps2027/2027-call-for-papers.html)
requires a 500-word abstract by **October 1, 2026 AOE** and a ten-page,
double-anonymous paper by **October 8 AOE**. From September 23 there are eight
days to the abstract and fifteen to the paper.

### Current recommendation: conditional submission

An IPDPS submission is viable only as a focused SSSP scheduling paper. The
present evidence does not support a broad adaptivity paper. Use this thesis:

> On sparse high-diameter graphs, distributed asynchronous SSSP loses time to
> speculative work and long uninterrupted local drains. Process-wide priority,
> batched removal and bounded heap slices reduce that work and restore message
> interleaving, producing a reproducible CPU-cluster win on meshes; roads expose
> the remaining global-round limit.

This is narrower than the original project goal, but it is honest and gives
the road loss explanatory value. Describe metadata gates as automatic policy
selection. Reserve “adaptive” for decisions made from live execution state;
the current winning profile does not make such a decision.

Make the final go/no-go call on **September 26**. Submit only if all of these
are true:

1. The fixed candidate passes the frozen-binary RMAT regression gate on
   Frontier at 16 nodes (see *Machine decision* below); an Anvil gate is
   optional replication. *Status 2026-09-24: NO-GO by the recorded rule on
   `rmat26` and `rmat27` (current-state §10); author decision pending.*
2. The matched spanning-tree screen either closes with a clear result or is
   omitted; it must not remain an unresolved dependency in the paper. *Closed
   2026-09-24 on Frontier: `SPANTREE=ON` kept, small effect (current-state §10).*
3. A fixed-candidate strong-scaling figure and its work/round explanation can
   be completed without tuning on held-out sources.
4. The new contributions can be separated cleanly from the IA³@SC24 paper:
   process-shared ordering, batched removal, heap slicing, the scheduler
   sensitivity result and the cross-system evaluation.
5. At least a complete six-page draft exists, with the main figures populated
   and the negative road/scale-free results included.

If any item fails, skip IPDPS rather than submit an adaptivity claim the data
does not establish. Continue toward SC27 with a real online policy and larger
scale as the distinguishing contribution.

The beyond-memory road result is an SC27 path, not a new dependency for the
October IPDPS submission. Do not promise it in IPDPS unless the measured curve,
memory audit and distributed comparison are complete before the data freeze.

## Work through September 26

**Machine decision (2026-09-24).** Anvil jobs have waited in the Slurm queue
too long to gate the September 26 decision, so every remaining gate runs on
Frontier. Anvil cells become optional replication if its queue moves; nothing
waits on them. Two measured facts make this workable: at 16 Frontier nodes the
launch bimodality almost vanishes (1 slow launch in 160, current-state §9), and
Frontier's campaign runtime is already built with `SPANTREE=ON`, so the
spanning-tree question can be answered there by building the opposite runtime.
The cost is gate resolution: repeated 16-node RMAT launches vary 4–9%
(coefficient of variation), so the gate uses 16 launches per arm and resolves
regressions of roughly 5% or more. State that limit in the paper.

### P0. Freeze documentation and artifacts — September 23

- Use [configurations.md](configurations.md) for all builds and
  [optimization-ledger.md](optimization-ledger.md) for mechanism status.
- Give every binary an immutable manifest and full SHA-256. Preserve job
  configs, raw logs and parser revision with each result.
- Tag one candidate revision after the two bounded screens below. No source
  changes enter the evidence campaign afterward except correctness fixes.

### P1. Run two bounded mechanism screens — September 23–24

1. **Spanning tree, on Frontier.** Frontier's runtime has `SPANTREE=ON`;
   build the same Reconverse source with `SPANTREE=OFF` and compare the two on
   the preregistered road arms. Accept `SPANTREE=ON` as the campaign setting
   only if it lowers round cost in two allocations without changing work,
   correctness or the selected algorithm settings. Stop after this screen. The
   Anvil `acic_span` screen is optional replication.
2. **Frontier progress.** *Not run.* The launch modes nearly vanish at 16
   nodes, where the remaining gates run, so the screen is no longer a
   dependency. It stays the bounded hypothesis for the 8-node modes.

Neither screen licenses a parameter search. Their combined purpose is to close
known runtime uncertainty before freezing the paper candidate.

### P2. Complete the submission gates — September 24–25

- Run the frozen candidate/R0 regression suite on Frontier at 16 nodes and
  8 × 7 (the Frontier C6 candidate's layout): `rmat25`, Orkut, `uniform25`,
  `rmat26` and `rmat27`, four held-out sources, frozen R0 with a repeated R0
  control, 16 launches per arm, two allocations. The Anvil package
  (`scripts/anvil/rmat_gate.sbatch`) is optional replication.
- Run one fixed-candidate strong-scaling curve for `mesh26-z`: 1, 2, 4 and 8
  Anvil nodes, plus the existing equal-PE Frontier point. Report time, attempts
  per edge, rounds, messages and parallel efficiency. Keep layout policy fixed
  by the documented machine mapping. *Frontier curve done at 1–16 nodes, jobs
  5536321/5536322 ([current-state §8](current-state.md#8-the-fixed-mesh-candidate-strong-scales-and-each-mechanism-is-causal)),
  and for `mesh24-z` in 5538412/5538413. Under the machine decision this is
  the paper's curve; an Anvil curve is optional replication.*
- Produce the causal ablation at the scale where the win appears: local queue;
  nearest queue without batching; batch 8; batch 8 plus slice 8. Reuse accepted
  cells where protocols match. New cells use training sources for selection
  and held-out sources once for confirmation. *Done at 16 Frontier nodes in the
  same jobs; the arms are fixed, so no selection stage was needed.*

### P3. Draft in parallel — September 23–26

*Superseded by "Paper layout" in the IPDPS plan.* Build the paper around four figures/tables:

1. End-to-end time versus GAPBS and distributed CPU baselines, separated by
   mesh, road and scale-free regimes.
2. Strong scaling and attempts per edge for the frozen mesh candidate.
3. The queue/batch/slice causal ablation.
4. Road's work-versus-round tradeoff, showing why near-minimal work does not
   yet beat GAPBS.

The limitations paragraph belongs in the main evaluation: CPU only; one
winning graph class; no demonstrated live-adaptation gain; compact-wire scale
limits; Frontier RMAT instability.

## September 27–October 8 if the gate passes

*Superseded by the IPDPS 2027 submission plan (2026-09-28) above.*

| Date | Deliverable |
|---|---|
| Sep 27–28 | Freeze all numerical tables and figures. Complete related work and the explicit IA³@SC24 delta. No new optimization. |
| Sep 29–30 | Complete a ten-page draft, internal review, artifact inventory and 500-word abstract. |
| Oct 1 AOE | Register the abstract. |
| Oct 2–4 | Revise argument, methods and figures. Re-run only a corrupt or missing accepted cell. |
| Oct 5–6 | Reproducibility audit: every number traces to a manifest, job and parser output; anonymity and prior-publication audit. |
| Oct 7 | Final technical and format review. |
| Oct 8 AOE | Submit. |

### Submission evidence checklist

- Candidate and baselines have immutable manifests and consistent timing
  boundaries.
- Every timed solve passes the independent digest; failed and slow-mode runs
  remain visible.
- Training sources select settings and held-out sources appear only in the
  confirmation cells.
- Two allocations support every headline result, and every figure traces to a
  raw record and parser revision.
- The main paper includes road and scale-free losses, the CPU-only scope and an
  explicit comparison with the IA³@SC24 contribution.
- No claim credits the admission threshold. Adaptivity claims cite the gated
  flush (F11) and the tiling rule (F15b) (revised 2026-10-02; this read "no
  title or claim says live adaptation caused the mesh result").

*Superseded by "Paper layout".* A practical ten-page budget was 1 page introduction, 1 background/predecessor,
1.25 diagnosis, 1.5 mechanisms, 1 method, 3.25 evaluation, 0.65 related work
and 0.35 limitations/conclusion.

## Competitor priority

Finish a small defensible matrix before adding another implementation.

| Priority | Implementation | Role |
|---|---|---|
| P0 | GAPBS SSSP, one node | Primary optimized shared-memory reference on every graph that fits. Jointly tune thread count and delta on training sources. Multi-node ACIC must still report this comparison. |
| P0 for road | Wasp | Modern multicore asynchronous SSSP comparator. Run the artifact on exactly the same topology, weights, sources and timing boundary. GAPBS proximity does not establish Wasp proximity. |
| P0 | RIKEN Graph500 SSSP | Primary distributed CPU competitor on scale-free graphs and every high-diameter input its weight representation supports. Tune rank/thread geometry and delta. |
| P0 | ACIC frozen predecessor / one-axis ablations | Establish novelty and causal gain over the workshop system. This is as important as an external baseline. |
| P1 | Galois SSSP | Independent optimized shared-memory reference. Add after the P0 matrix is complete. |
| P1 | GBBS/Julienne or GraphIt priority SSSP | Add one if it supplies a stable exact-input comparison after the GAPBS/Wasp matrix is complete. |
| P2 | Gluon-Async | Retain existing distributed comparison for continuity, but do not spend deadline time extending it; GAPBS and RIKEN are stronger decision baselines. |
| P2 | cuGraph multi-GPU or Gunrock | A clearly labeled cross-hardware context row if matching hardware and graph semantics are readily available. Literature coverage is required; a direct GPU run is optional for a CPU-cluster paper. |

Do not describe ACIC as the fastest SSSP system. Compare within hardware class,
state device/node resources, and discuss strong GPU SSSP systems in related
work.

## Graph support priority

Use GAPBS-style exact positive-integer weighted SSSP as the primary contract.
This is not an official Graph500 submission and its rates must not be compared
with Graph500 leaderboard GTEPS.

| Priority | Graph family | Required cells |
|---|---|---|
| P0 | Synthetic 2-D meshes | At least three sizes, including `mesh26-z`; training and held-out sources; strong scaling through the largest useful node count. This is the proven favorable regime. |
| P0 | Real roads | `road-usa-z` plus a second held-out region such as Road-EU, using native positive weights in at least one row. Show the common-input loss and round diagnosis. |
| P0 for scale | Road-like size series | A reproducible family with stable degree, weight and source rules, spanning comfortable one-node fit, the measured memory boundary, 2–4× beyond it and at least one 8×-beyond target. Keep the graph semantics fixed as size grows. |
| P0 | Scale-free / small-world | RMAT or Kronecker at several scales, Orkut and one larger social/web graph. These are required counter-regimes, even when ACIC loses. |
| P1 | Uniform random | Keep `uniform25` in regression and add a larger scale only when it tests a stated hypothesis. |
| P1 | Directed and disconnected real graph | At least one of each before an SC27 generality claim; define loops, parallel edges and unreachable vertices. |
| Stress | Near-zero, highly skewed or floating weights | Separate numerical/algorithm stress study. Do not mix it into the exact-integer headline matrix. |

A mesh-only result can support a characterization paper, but not a broad graph
algorithm performance claim. For a stronger systems paper, the next meaningful
target is a real high-diameter graph, not another synthetic mesh variant.

## Road performance and capacity claim

Treat the road result as three different questions. Do not combine their
language or denominators.

| Question | Evidence | Allowed claim |
|---|---|---|
| Who is faster on the same graph? | Same topology, weights, sources and timing boundary; tuned GAPBS/Wasp and ACIC | A performance win only if ACIC is faster. If ACIC is 1.5× slower, report it as within 50% rather than “matching.” |
| Who can solve the larger graph? | Measured peak memory and completion beyond the shared-memory implementations' demonstrated limit | A capacity win. An out-of-memory or unsupported run is not an ACIC speedup. |
| Does distribution pay at scale? | Fixed-graph strong scaling, size-proportional scaling and a distributed competitor at the largest common scale | A distributed scalability or performance win, depending on the measured result. |

A defensible target claim is:

> ACIC remains within 1.5× of optimized multicore SSSP on the largest common
> road input, while scaling exact weighted SSSP to an input at least four times
> beyond the measured single-node memory requirement with useful multi-node
> scaling.

Strengthen “capacity” to “distributed performance” only when a distributed
baseline completes the same large graph and ACIC is faster. If ACIC alone
completes it, call the cell a capacity demonstration.

### Required experiment matrix

1. **Common-input crossover.** Run GAPBS, Wasp and ACIC over several sizes that
   fit one node, including the largest comfortable fit and a near-memory-limit
   cell. Run ACIC on one and multiple nodes. This shows whether distributed
   overhead stays bounded as the useful problem size grows.
2. **Beyond-memory series.** Measure at least 2× and 4× the single-node memory
   requirement; target 8× or more for the headline capacity cell. A few percent
   beyond one node is not persuasive because a larger-memory server could erase
   the distinction.
3. **Distributed comparison.** Run RIKEN or another exact weighted distributed
   SSSP implementation on every large graph it can represent. If RIKEN cannot
   preserve the road weights, select or implement one alternative before making
   a distributed performance claim.
4. **Scaling axes.** Report fixed-graph strong scaling and size-proportional
   scaling. Since road diameter and controller rounds may grow with size,
   include work/edge and critical-path depth rather than requiring constant
   weak-scaling time.

### Memory and measurement audit

The capacity claim requires evidence that ACIC distributes state rather than
replicating the limiting structures. Report:

- measured peak RSS per node and total memory;
- bytes for CSR, distances, queues, aggregation and replicated metadata;
- predicted versus measured memory at every size;
- vertices, stored directed edges, reachable fraction and graph diameter or a
  comparable shortest-path-depth measure;
- solve time, graph construction/loading, preprocessing and end-to-end time as
  separate quantities;
- attempts per edge, rounds, messages, failures and out-of-memory outcomes.

Use identical positive weights, direction, source set, distance type and graph
conversion for all implementations. A Wasp result with regenerated weights is
not directly comparable with ACIC on native road weights.

### Road success gates

- **Local-efficiency gate:** on the largest common input, ACIC is at most 1.5×
  the faster of tuned GAPBS and Wasp, under the same solve boundary.
- **Capacity gate:** ACIC completes a verified graph whose measured memory
  requirement is at least 4× the available memory of the comparison node, with
  no full graph or distance-vector replication per node.
- **Scaling gate:** time decreases over a meaningful fixed-graph node range, or
  the size-proportional curve retains useful throughput while work/edge and
  rounds have an explained trend.
- **Distributed-performance gate:** ACIC beats a distributed baseline on the
  largest common graph. This gate is optional for a capacity claim and required
  for a distributed performance claim.

## After the IPDPS decision: path to SC27

### Gate A — make adaptivity real, October–November 2026

Replace the road-specific width with an online rule derived from observed
distance range, active-work distribution and measured round cost. It must
choose among ordering window and heap-slice regimes without graph-name rules.
Compare it with the best fixed setting selected separately for each graph.

Pass when the live policy is within 10% geomean of the per-graph best fixed
policy, avoids catastrophic cases, and beats one global fixed policy across
mesh, road and scale-free families. Otherwise publish the scheduling
characterization and stop calling ACIC adaptive.

### Gate B — remove scale blockers, November–December 2026

- Extend vertex IDs, edge counts and compact messages for the selected large
  scales; add overflow checks and an independently verifiable certificate.
- Stream or parallelize graph construction and eliminate all per-node full
  graph copies before requesting large allocations.
- Establish memory models for ACIC and RIKEN, then test a scale that exceeds
  one-node memory before attempting record-scale runs.
- Port and validate Wasp on the exact road inputs while they still fit one node;
  record its peak memory and failure boundary rather than assuming it from the
  implementation model.

### Gate C — large-scale evidence, January–February 2027

First run the road crossover and beyond-memory matrix above. Progress through
larger synthetic scales rather than jumping directly to a machine record:

1. scale 29–31 at 16/32/64 nodes;
2. scale 33 at 128/256/512 nodes if memory and correctness gates pass;
3. scale 35 or 1,024+ Frontier nodes only after the previous curve shows
   useful scaling and a distributed baseline can run the largest common graph.

If ACIC alone fits, label the run a capacity demonstration. Claim a speedup
only on a graph and scale completed by the comparator under matching semantics.
Do not use aggregate edge rate to hide increasing solve time or critical-path
rounds.

### Gate D — broaden algorithms, after the SSSP mechanism is established

Port one algorithm whose control needs a different signal, most likely BFS or
connected components. The goal is to test whether the runtime policy transfers.
Do not add algorithms merely to increase the benchmark count.

## Stop rules

Stop SSSP optimization for the current submission when any one holds:

- a proposed change misses its preregistered effect in two allocations;
- the same gain is available from a fixed setting with no live-state input;
- a gain moves cost to another required family or breaks the regression gate;
- measurement variation is larger than the claimed gain and cannot be removed
  by the bounded runtime screens;
- the candidate remains behind GAPBS/Wasp on the current road after the
  spanning-tree screen. Freeze small-road tuning and move to the crossover and
  capacity experiment rather than tuning another width.

For the longer program, stop claiming a general adaptive advantage if Gate A
fails. A missing distributed comparator limits the result to capacity; it does
not invalidate a verified capacity result. Stop the extreme-scale performance
campaign if fixed-graph time does not improve over a meaningful node range. A
capacity-only endpoint may still be reported, but it must not be called a
speedup. A clean negative characterization is more useful than an open-ended
sequence of settings.

## Gate A checkpoints

| Checkpoint | Decision evidence |
|---|---|
| Sep 26, 2026 | IPDPS go/no-go using the five conditions above. |
| Nov 30, 2026 | Online-policy gate: live controller versus per-graph best fixed and one global fixed. |
| Dec 31, 2026 | 64-bit/streaming readiness, verified memory model and GAPBS/Wasp common-input crossover. |
| Jan 31, 2027 | Road capacity gate at 4× beyond single-node memory, with strong/size-proportional scaling and a distributed-baseline attempt. |
| Feb 15, 2027 | Strong-scaling, capacity, competitor and graph-family matrix sufficient for SC27 scope. |

### Scaling entry condition

Enter a larger scale only when the previous scale is correct, fits the memory
model, and is faster than the next smaller accepted scale or answers a stated
capacity question. Do not use more nodes solely because they are available.


## Delta road round-cost investigation — September 24

At the user's request, investigate the remaining 500–800 rounds at roughly
0.2 ms per round using newly rebuilt Charm++/Reconverse/SSSP and the old
scheduler. This is phase attribution, not another width/cap sweep. The
spanning-tree screen remains closed. Runtime identities are recorded in
[configurations.md](configurations.md#delta-runtime-refresh-september-24).

The preregistered protocol is
`benchmarks/delta-road-rounds-protocol.json`. Run
`scripts/delta/road_round_probe.sbatch CAMPAIGN --trace`, overriding `-N` for
the multi-node cell, only after the corresponding correctness gate passes.

1. Validate the new production and quiet-round binaries against serial
   Dijkstra on one and two nodes. All runs use `+old-scheduler` (the actual
   spelling accepted by Reconverse).
2. On one node and eight nodes, use two training road sources, 16 processes
   × 7 workers per node, nearest queue, batch 8, slice 8, width 131072,
   process share/reader tile/hub hints auto, slack control off. Interleave
   production, quiet-round and repeated-production arms, one warmup and
   three measured launches, retaining the graph between sources.
3. Separately time controller/logging/broadcast-call work and worker
   threshold setup, hold release, flushing, queue dispatch, histogram
   preparation and contribution calls. Run work counters and one single-source
   Projections trace separately from performance builds. Check their timing
   perturbation against production.
4. Calibrate an empty array-broadcast/sum-long-reduction cycle at 8 × 15
   and 16 × 7 per node, with 11 versus 267 longs (the real histogram size),
   100 warmups and 1,000 measured rounds, three launches each. Validate
   placement, sequence and the entire payload.

The protocol records quantitative decision rules. Substantial root logging
would motivate an output-only intervention; a high unloaded round floor
would motivate collective/scheduler work; a low floor would direct effort
toward threshold handling or queue delays under load. Do not add parallel
PE-times as wall time or subtract an unloaded microbenchmark as communication
overhead. Quieter output may change work and round count as well as cost.

Accept an intervention only after a matched comparison and a second
allocation, full distance digests, work/queue accounting and regression
checks. A runtime rebuild alone is not an accepted performance improvement.

The first Delta allocation (22354907) rejects logging as a useful
intervention and finds substantial loaded queue delay. A trace-backed
follow-up tests `ACIC_COALESCE_HEAP`: allow only one pending shared-heap
callback per worker instead of adding one each controller round. Predictions
are recorded in `benchmarks/delta-heap-coalesce-{road,mesh}-variants.json`: at
most one pending callback, at least 25% less p90 controller callback delay,
road speedup 1.05–1.20× with work within 20%, and no resolved mesh regression.
`scripts/delta/heap_coalesce.sbatch` interleaves the frozen production,
prototype and repeated control plus separate work builds, then traces one
road source and audits all PEs' callback backlog. The two-node correctness
gate is 22355072. Keep the prototype disabled in production pending results.

The road-only portion of 22355092 completed, but its audit then rejected the
driver's 16-process default because the audit assumed eight. The saved data
pass with explicitly matching defaults; the missing mesh/traces resume in
22355144 (22355117 was cancelled while pending to add a matched baseline
trace). Road shows no net one-node speedup despite approximately four times
faster rounds: the loop polls roughly four times more often. Separate useful
threshold advances from repeated controller polls in the distributed result.
The prototype remains disabled in the workspace production binary.

Eight-node coalescing job 22355150 depends on both attribution job 22354948
and continuation 22355144. This tests the distributed regime, where earlier
measurements found substantially more idle time. It does not turn the missed
one-node speedup prediction into a pass. The trace parser now computes
per-process-pair minimum delays in one pass instead of rescanning every
message for each pair; exact equivalence was checked for empty, sparse, dense
and signed-delay inputs up to 128 processes before the large traces.

At the user's request, use `cpu-interactive` for eligible pending tests.
Its current limits are four nodes, one hour, one running job and two submitted
jobs per user. Move continuation 22355144 in place so its existing downstream
dependency remains valid. Four-node attribution cell 22355241 uses the same
frozen binaries, per-node layout, sources, checks and 20-minute limit after
that continuation succeeds. This intermediate scale gives earlier evidence
of how the round floor changes with distribution; retain the eight-node
endpoint and its coalescing comparison in `cpu`. The configured interactive
CPU billing weight is twice that of `cpu`; queue priority is higher, but an
earlier start is not guaranteed. Keep `+old-scheduler` on every run.

The completed continuation 22355144 rejects general heap coalescing: its
mechanism works (maximum pending callbacks 94 → 1; controller p90 wait
181 → 31 us), but mesh speedup is 0.864/0.842×, outside controls and below
the 0.95 floor. Road already missed its speedup prediction. Keep this version
disabled; skip second-allocation and held-out acceptance runs. The existing
eight-node comparison remains a bounded test of the distributed mechanism,
not a route to accepting this version as a general optimization.

Four-node 22355241 supplies valid production/quiet/control timing and unloaded
cycle data, but stdout interleaving broke the subsequent phase audit. The
driver now captures profile output per rank; use that fix in pending eight-node
attribution 22354948. Retain all completed timings; rerun only missing four-node
diagnostics if the eight-node evidence leaves a relevant question unresolved.
At four nodes, the 267-long empty cycle averages about 193 us at 16 × 7
versus 101 us at 8 × 15. These are unloaded results only; the September 25 decision below retains
16 × 7 based on the user's prior full-solver tests. Distinguish
useful threshold advances from repeated controller polls and report work as
well as rounds. Do not infer a solve speedup from the unloaded benchmark or
lower callback latency alone. No new jobs were submitted in this review.


### September 25 decision after the eight-node jobs

Both jobs completed with all gates valid: attribution 22354948 has 29 solve
digests, 12 empty-cycle checks and 1,792 PE/source profile records; comparison
22355150 has 82 solve digests and 32 work ledgers. Preserve the partial
four-node result as such; its missing diagnostics need no rerun for the
current decision because the corrected eight-node capture succeeded.

Close the current coalescing prototype. Road speedup 1.043/0.962× misses
its prediction, with 69/81% extra work above the 20% bound. Mesh's
0.993/1.114× at eight nodes does not undo the one-node regression. Suppressing
heap backlog is insufficient: in the matched eight-node trace, controller
p90 barely changes (37 → 36 us), and total heap callbacks increase.

**Keep 16 × 7 fixed.** The user has already tested other layouts on other
machines and found this best. The unloaded 8 × 15 result is not a full-solver
win and also uses more workers. Drop the suggested layout screen. Preserve
`+old-scheduler`, width 131072, nearest queue, batch 8 and heap slice 8.

Audit progress before changing round cadence. Existing logs show that about
half the baseline rounds retain the threshold, but almost all of those still
create or retire updates. Do not label them empty or skip them without a
correctness argument. The current ordinary path queues hold release and
heap draining, then contributes its histogram before those callbacks run.
That is a concrete potential source of delayed feedback to the controller.

The next bounded prototype should test **contributing after one local work
turn**, instead of immediately after queueing that work. Keep heap work
bounded by the existing slice, preserve asynchronous communication progress,
and require exactly one contribution from every PE per epoch, including idle
PEs. Keep the current stable-count termination check and one controller epoch
in flight. Do not wait for queue exhaustion or global quiescence; do not add
arbitrary timer delays or combine this with rejected coalescing. The old
node-controller/interval screen remains closed.

Preregister a prediction of 15–30% fewer road rounds and 1.05–1.20× solve
speedup, with edge work within 20%; these are hypotheses, not achieved gains.
Test serial correctness and accounting first, then the two training sources
on one node with matched production/control arms, one warmup and three
measured launches. Check time, rounds, threshold changes and edge attempts
separately; fewer rounds without lower time is a failure. Require road
speedup at least 1.05× beyond control variation and mesh speedup at least
0.95× (within control noise if below 1), then a second allocation and held-out
confirmation before acceptance. Preserve the eight-node endpoint in `cpu`
if the mechanism advances to a distributed test; the user declined reducing
it to four nodes. Stop this prototype if its prediction fails rather than
opening another cadence sweep.

The hypothesis and fixed settings are recorded in
`benchmarks/delta-road-rounds-protocol.json`. The prototype is not implemented
or queued. No new jobs were submitted in this review, and the queue is empty.


## Delta mesh28-z versus Wasp: one-node attribution (2026-09-25)

The user's next priority is the one-node mesh deficit. Keep ACIC at 16 × 7
with `+old-scheduler`, process sharing/reader tiling auto, nearest queues,
batch 8 and slice 8; use the refreshed production/tracing/shmem runtime.
The road contribution-placement prototype remains unimplemented.

`benchmarks/delta-mesh28-wasp-protocol.json` freezes a bounded Wasp search
(64/96/128 threads × delta 1024/4096/16384), two training sources, and four
held-out sources with three randomized paired timing repetitions. Build
Wasp from the checksum-verified SC25 artifact and use its existing digest
adapter. Both engines read the same deterministic Morton mesh (seed 1);
six independent GAPBS-reader Dijkstra references gate every solve. Run only
one `cpu-interactive` node at a time. Preparation job: 22378324.

Separate binaries collect ACIC queue/edge counters, Wasp's existing
COUNT_RELAX counter, and a full-solve Projections trace bracketed by plain
controls. Wasp counts both its low-degree pull and push inspections; do not
mislabel that as only outgoing push attempts. Compare measured work and
callback costs before proposing an implementation change. Trace time shares
are PE-time attribution, not additive critical-path fractions.

Campaign: `/work/hdd/mzu/rao1/acic-mesh28-wasp-20260925`. Hypothesis: heap
work and runtime scheduling, rather than controller entry time alone, dominate
this larger mesh; ACIC remains behind Wasp. These are predictions, not results.


### Mesh28 attribution result and next gate

Comparison **22378381** completed: 62/62 full digests, conserved work, four
held-out sources with three timing repetitions. ACIC takes 3.53–3.84 s by
source median versus Wasp 0.930–0.974 s (speedup 0.246–0.276×). The full
112-PE Projections trace adds 2.57% time and shows 82% of PE time in
`process_heap`, 72.6M callbacks, and only 0.26% in threshold entries. Queue
sampling estimates 52.5–52.8% of total PE time in push/pop calls. Wasp does
more outgoing inspections in the counter runs despite finishing faster.
The attribution hypothesis is supported; controller entry execution and
inter-process sending are not the main measured costs on this input.

Move an **application-queue cost prototype** ahead of road contribution
placement for this one-node investigation. Keep 16 × 7 and +old-scheduler.
Use producer-private chunks within original distance buckets, publishing
bounded chunks for stealing; all privately buffered work remains charged,
visible to termination, and subject to admission-generation checks. Do not
change runtime scheduler queues or merge the rejected coalescing prototype.
A focused PC sample can first distinguish map/heap operations, locking, TLS
and vertex lookup inside the now-established hotspot.

Before implementation measurements, freeze an exact variant and prediction.
Suggested first gate: at least 1.20× speedup on both training sources beyond
matched-control variation; treat scans as an explanatory metric rather than
requiring them to fall. Require independent digest and queue conservation,
then four held-out sources and another one-node allocation before acceptance.
If callback overhead remains material, separately test a larger bounded heap
slice with batch 8 held fixed; do not combine interventions in the first A/B.
Recheck multi-node progress and the distributed mesh endpoint before changing
the paper's mesh profile. No solver intervention was implemented in this
attribution task. Full results and trace paths are in current-state §16.


### Queue step implemented: bounded private chunks (2026-09-25)

`ACIC_PROCESS_CHUNKS` selects an experimental application queue. Producers
keep private partial chunks, publish full 64-item chunks, and steal published
chunks under per-producer mutexes. Original histogram charges remain live
through every transfer. Admission is rechecked after controller changes; a
partly admissible old overflow bucket cannot hide its eligible work. The
existing heap remains the default build.

The first FIFO prototype passed small correctness but lost distance order
inside the unbounded overflow bucket: job 22379048 was cancelled after more
than 188B created updates and 129.8 seconds, without a completed mesh answer.
The revised queue subdivides original buckets by native distance and gives
earlier published bands precedence over later private work. Width 4096 gave
only 1.04/1.02x speedups (job 22379156), with about 4 scans/edge versus 1.4–1.5.
A preregistered narrower-band screen (job 22379228) selected width **256**
over 1024 on both training sources; full values are in
`onenode-data/delta-mesh28-chunks-training.json`. Its speedup exceeds 2x.
The chunk size stays 64 and queue batch/heap slice stay 8 for this step.

The original queue tests and new admission, overflow, source reuse, and
8-thread exact-once stress tests pass; AddressSanitizer/UBSan and
ThreadSanitizer pass. Each queue screening gate passed 32 serial checks and
32 Bellman certificates on one node at 16 × 7. All 24 width-4096 and 32
narrower-band full-graph screening solves match the independent references.
This is training evidence, with held-out confirmation pending. No new
multi-node run is authorized by the current one-node resource limit.

Step 2 now tests heap slices 8/32/64 on the frozen 256-band queue, with queue
batch 8 held fixed. Step 3 will profile the selected combination, bracketed
by production controls, and validate original, chunk-only, and combined
settings on four held-out sources. Preserve 16 × 7 and +old-scheduler.


### Heap-slice step selected (job 22379316)

All 32 training solves passed. The frozen 256-band queue with slice 64 gives
medians **1.343232 / 1.370039 s**, versus slice-8 base medians
**1.560071 / 1.638582 s** and repeated controls **1.615561 / 1.640856 s**.
This is **1.16 / 1.20x** over the base, beyond the larger 3.6% duplicate-control
spread on the first source. Slice 32 gives 1.387383 / 1.419793 s. Choose **64**
for held-out validation; it is the largest tested value, not an established
optimum. Queue batch remains 8. No runtime scheduler or global default changes.

`benchmarks/delta-mesh28-selected.json` freezes the selected binary/settings.
The final one-node allocation compares the original, chunks with slice 8,
and chunks with slice 64 on four held-out sources, with a duplicate original
control. It then rechecks the fixed Wasp reference (128 threads, delta 4096),
full work ledgers, paired solve-window PC samples, and an optimized Projections
trace. Original and selected PC binaries each run with timers on/off and
production controls; profile times never replace production times.


### Three-step mesh investigation completed (job 22379656)

The held-out confirmation passes all 99 full-graph solves and the selected
small-graph gate (32 serial checks plus 32 certificates). Private chunks with
band 256 and slice 64 give 2.53–2.55× over original ACIC, reducing time by
60.5–60.7%, and reach 0.608–0.704× over freshly remeasured Wasp. Both queue
and slice gains survive on all four held-out sources. Keep this as an opt-in
one-node mesh profile; the original heap remains the default. The code is
committed in `8a6b02c`, slice selection in `98b80ea`; current-state §17 and
`onenode-data/delta-mesh28-optimized-22379656.json` contain the full evidence.

PC captures locate the removed cost in mutex/futex operations (26.6–27.0%
to 0.37–0.38% of samples). Remaining candidates are duplicate local ownership
lookup, repeated process/runtime access and TLS. Queue-call samples estimate
53% → 18% PE time; the new trace has 10.1M rather than 72.6M heap callbacks.
Trace overhead is 1.9%; PC sampling overhead is 7.9–10.9% on the selected
version, so neither instrumentation time substitutes for production timing.

Stop this bounded queue/slice screen. The next focused intervention, if the
investigation continues, should pass known destinations through local updates
and cache stable process ownership with correct lifecycle refresh. Keep a
TLS runtime A/B separate. Do not infer a guaranteed gain from sample shares.
Distributed progress/scaling and other sparse graphs must be checked before
broad promotion; no larger-node jobs were submitted. This permits a stronger
single-node engineering baseline while keeping the paper centered on ACIC's
distributed capabilities and crediting established chunking techniques.

During analysis, the user requested an upstream pull. Merge `69adfc1`
incorporates upstream through `2bacfc4`; it merged without conflicts and
passed changed-file Python/shell/JSON syntax checks. It changes Frontier
harness/input-preparation files, not the solver. The running job used frozen
binaries and a frozen harness, so its provenance remains unchanged. Local
unrelated files and the pending report changes were preserved.


### Road/uniform transfer check queued (2026-09-25)

The user requested the same one-node comparison on road-usa and uniform.
Job **22392217** uses one exclusive `cpu-interactive` node, 16 × 7 and
`+old-scheduler`, with the existing `road-usa-z` / `uniform25` graphs and
independent references. Reuse the frozen original and private-chunk binaries
from the mesh study: compare original/slice 8, chunks/slice 8, chunks/slice 64,
and a duplicate original control. Chunk band 256, chunk size 64 and batch 8
stay fixed; road retains its established bucket width 131072.

Uniform's average degree 32 disables process sharing under `auto`, so chunk
storage and the configurable shared-heap slice should be inactive. Expect no
systematic uniform change beyond control variation; do not present this as a
positive test of the chunk mechanism. Road may remain round-bound despite
cheaper queue operations; no speedup is assumed before measurement.

Wasp's joint training grid is 64/96/128 threads, with road delta
8192/32768/131072 and uniform delta 16/64/256/1024. Repeat its best two settings
once on both training sources, then freeze before testing. Every arm receives
one warmup and three measured launches on each of four held-out sources,
randomly interleaved within each repetition. Base and selected cost builds
also run on the first two held-out sources. Every solve must pass the full
reference digest; diagnostics must conserve queue and edge work. There are
111 planned road and 117 planned uniform solves, including training/smoke.

Protocol: `benchmarks/delta-chunks-road-uniform-protocol.json`. Campaign:
`/work/hdd/mzu/rao1/acic-chunks-road-uniform-20260925`. The driver freezes
binary and graph hashes and records commands, effective process-sharing mode,
work and timing. This is a transfer/regression check in one allocation; a
small gain within duplicate-control variation is not an adoption result.
No other node is used concurrently and no solver default is changed.


The initial uniform Wasp search selected 128 threads / delta 16, the lower
boundary, on training sources. Before interpreting its held-out comparison,
a sequential follow-up checks delta 16 versus 4 twice on both training
sources at the selected thread count. Only if 4 wins, check 4 versus 1;
stop there. If selection changes, remeasure original ACIC, chunks/slice 64
and Wasp together on the four held-out sources (warmup plus three repeats).
This is baseline verification, not another ACIC tuning pass; no held-out
source is used to choose delta. Follow-up job **22398569** depends on completion of
22392217 so that only one node runs at a time. Protocol is archived at
`protocol/wasp-boundary.json` in the same campaign.


### Road/uniform result: retain mesh-only activation

Both jobs completed, sequentially on cn115: **22392217 (228/228 valid)** and
**22398569 (65/65 valid)**, with all eight work ledgers conserved. Road
chunks/slice 64 has speedup **0.719–0.836×** over original, beyond duplicate
control variation of 0.959–1.041×; chunks/slice 8 is worse (0.612–0.671×).
Reject this road transfer and keep the original heap. Queue calls are cheaper
but diagnostic idle time rises from 13–18% to about 68%, and production rounds
rise 2.58–3.53×. Nearly all extra unchanged-threshold rounds still process
updates, so removing nominally "empty" rounds is not a supported fix.

The next road-specific hypothesis, if pursued, is to measure private partial
occupancy/publication and make short chunks available when peers lack work.
Native band 256 was mesh-tuned; thin road frontiers and larger native weights
may prevent full 64-item chunks from forming. This explanation is an inference
from the queue code, idle time and empty-pop behavior, not a completed causal
ablation. A wider road band is a separate experiment. Do not make this queue
a global sparse-graph default or tune on the held-out sources just measured.

Uniform shares no process state under auto, so the chunk and slice changes
are inactive. Both allocations show no systematic effect: speedups over
original **0.990–1.029×** and **0.978–1.011×**. The Wasp boundary check chooses
128 threads / delta **4**, with delta 1 slower, and repeats the held-out
comparison in that allocation. Original ACIC is **1.103–1.211×** faster than
Wasp, and the chunk binary **1.079–1.206×**; this is evidence for the existing
uniform path, not for chunking. No additional jobs remain active or queued.

Full analysis: current-state §18. Archive:
`onenode-data/delta-chunks-road-uniform-22392217.json`, including both jobs,
all per-source medians, training selections, work counters, controller
progress classifications, and raw paths. The final report reaudit checks all
293 digests, recorded times, work ledgers and effective old scheduler.


### Road layout and reduced-core study (2026-09-25)

The user now explicitly requests road optimization with different process
layouts, including leaving cores idle; this supersedes the earlier fixed
16 × 7 preference for this study. The original heap remains the baseline.
The first screen uses the frozen validated binaries and unchanged runtime,
road width 131072, slice 8, queue batch 8 and `+old-scheduler`. No solver
default changes. One exclusive Delta `cpu-interactive` node at a time.

`benchmarks/delta-road-layout-protocol.json` specifies 14 layouts: 112-worker
16×7/8×14/4×28, 120-worker 8×15, 56-worker 8×7/4×14/2×28, 48-worker 16×3,
28-worker 4×7/2×14/1×28, 14-worker 2×7, and compact 8×7 within 64 cores /
4×7 within 32 cores. Other rank starts are spaced across 128 cores; a single
rank uses contiguous cores. The allocation still reserves the entire node.
Explicit CPU maps preserve a spare core in each process region; runtime
startup stays unbound as before. Every PE binding and effective process/PE
count is checked from runtime output. Compact/spread tests change worker
placement, without imposing a new memory-placement policy.

Use only the two training sources to choose: warmup plus two randomized
repeats per layout/source; repeat the best two and 16×7 three more times.
Choose by geometric mean of source medians in this confirmation phase.
Then freeze and compare selected layout, original 16×7, duplicate original
control and Wasp (64 threads, delta 32768, selected in the prior training)
on all four test sources: warmup plus three randomized repeats. Work builds
probe original/selected on the first two test sources. Each full solve must
match the independent digest; diagnostic queue/edge ledgers must conserve.
A gain of at least 5% beyond control variation warrants a second allocation;
this first screen alone will not promote a default. Prediction: reduced
parallelism may lower loaded round costs or contention on thin frontiers.

Separately investigate partial chunk publication and road-specific distance
bands at fixed 16×7, then combine only promising choices with the selected
layout. This distinguishes a placement gain from a queue algorithm gain.
Campaign: `/work/hdd/mzu/rao1/acic-road-opt-20260925`.


The first layout launch, **22400947**, stopped after 19 correct solves because
the harness incorrectly expected plural `processes` for the single-process
case. All raw outputs were revalidated with the corrected parser; timings
are excluded from selection. Replacement **22401042** reruns the entire
protocol on one node. The pilot rejection is archived in the campaign.

### Road queue ablation and combined confirmation

Commit `551571d` adds compile-time `ACIC_CHUNK_PARTIAL`. An unsuccessful
worker requests work using a per-producer atomic flag. Only the owner reads
its private queue and services the request at the next pop: publish up to
half the earliest band's items, capped at 32, provided admission allows
them and its published bin is empty. Donation uses the existing bin mutex;
peers never touch private storage. The owner retains work, and a singleton
remains private. Original histogram charges and admission checks are intact.
This is a bounded hypothesis test, not a generally enabled queue policy.

The work build additionally measures publication/take counts and items,
partial donations, peer-transferred items, requests/services, failed scans,
and private items/bands sampled every 1024 pop calls. These counters compile
out of production, including the extra reduction fields. They distinguish
fragmentation and sharing from a simple change in queue-call time.

`benchmarks/delta-road-queue-protocol.json` freezes six 16×7 arms: heap/slice8,
heap/slice64, and the 2×2 of full-only/requested-partial chunks with native
bands 256/65536, all chunk arms at slice64. Road admission width stays
131072. Add a duplicate heap/slice8 training control. Two training sources,
warmup plus three repeats, randomized. A candidate must improve both source
medians ≥5%, beat both baseline/control geometric means by ≥5%, and use
≤1.25× edge work per source; otherwise retain heap/slice8. No held-out
source participates in the choice. Wider bands may increase FIFO disorder;
partial sharing may lose to atomic/mutex overhead. Neither is assumed to win.

Before timing, every new production/cost binary passes empty/path/disconnected/
mesh graphs with four sources including reset, serial verification and
Bellman certificates, using road-scale weights. Existing queue admission,
overflow and 8-thread exactly-once tests pass, as do ASan/UBSan and TSan.
Large runs check the full independent reference and actual CPU bindings.
Cost builds require publication/take conservation in addition to the usual
queue/edge ledger. Instrumented timings never substitute for production.

After selection, the second allocation repeats baseline, queue-only,
layout-only, combined, duplicate baseline and Wasp on four test sources,
warmup plus three repeats. Layout was chosen only from the first allocation's
training sources. This both checks transfer and repeats the layout result
on another node allocation. Diagnose all six queue arms on the two training
sources and the combined profile if its layout differs. Runs remain
sequential on one exclusive `cpu-interactive` node with `+old-scheduler`.


Layout job **22401042** completed 164/164 valid solves on cn071 in 11:00.
16×7 wins the 14-layout training screen and three-repeat confirmation
against 8×14 (0.473509 versus 0.577971 s). Reduced-core choices are slower:
best 56 workers 1.70× baseline time; best 28 workers 3.04×. Keep 16×7.
Current-state §19 and `onenode-data/delta-road-layout-22401042.json` contain
the complete table, held-out controls, diagnostics and affinity checks.
The three original/selected/control arms are identical after selection,
so their small timing differences are measurement variation, not a gain.

Queue job **22401233** is submitted with `afterok:22401042`. All seven
new frozen binaries built successfully; exact source/binary/runtime hashes
and final sanitizer logs are archived. The original global defaults and
previous mesh binaries remain unchanged.


### Road time-focused follow-up (training-only decision)

The queue training phase freezes the original heap under its predeclared
≤25% edge-work growth gate. Preserve that result. However, **full-only
chunks/band 65536/slice 64** are **1.217–1.293× faster** on the two training
sources (geomeans: original 0.469996 s, control 0.468384 s, wide chunks 0.374699 s),
with **1.704–1.811×** as many edge attempts. Requested-partial wide chunks
are slower at 0.414447 s; heap/slice 64 also loses. Because the user's goal
is solve time, the work cap is too restrictive as the sole performance
decision. This motivates a separate time-focused confirmation, not a
retroactive change to the frozen work-capped protocol. No test-source
results select the queue.

`benchmarks/delta-road-time-protocol.json` freezes full-only band 65536.
Recheck that queue's layout on training sources at 16×7, 8×14 and 8×7: prior
best, runner-up at 112 workers and best 56-worker option. Warmup plus three
randomized repeats per training source. Change layout only with ≥5% benefit
on both sources. Then compare original 16×7, wide chunks16×7, wide chunks
at selected layout, duplicate original and Wasp on the four test sources,
warmup plus three repeats. Diagnose original/new/combined on two sources.
All 110 full solves require digest and affinity validation. This adds a
sequential one-node allocation, with no new binary or global default change.
Prediction: ≥1.10× held-out speedup beyond control noise, explicitly trading
extra work for faster per-operation execution; no distributed claim.


Queue job **22401233** completed 164/164 full solves and 112 serial/112
certificate gates, with all 12 work ledgers conserved. Full band 256
publishes only 4480–5376 updates per solve; full band 65536 publishes 9.83–10.15M,
of which 6.90–7.17M are taken by peers. Partial publication reduces idle
further but increases queue costs and loses to full-only at the wider band.
The conservative work-growth gate retains the original heap; its held-out
arms are identical controls, not a wide-band performance result.
Full details and the factual work/time tradeoff are in current-state §19.

Follow-up **22401351** is submitted afterok 22401233, with all 110 planned
solves on one node. At this checkpoint it waits for Slurm priority.
No more experiments are planned beyond this bounded confirmation.


### Road decision: accept a bounded one-node time profile

Final job **22401351** completed 110/110 solves on cn071 in 7:11. Across the
three completed studies, 438 full solves, 112 serial checks and 112 certificates
pass; all 22 full-graph diagnostic work ledgers conserve. All allocations
ran sequentially on cn071, and no jobs remain queued or running.

The selected full-chunk/band 65536/slice 64 profile gives **1.116–1.286×**
over the original on four test sources, geometric mean **1.187×**. It uses
**63.6–77.5% more edge work**, with more rounds, and remains **0.232–0.263×**
as fast as Wasp. The duplicate new arm also improves all source medians,
though it differs by up to 9.6% on one source; report the modest gain with
that variation. This confirms the time-focused prediction while preserving
the earlier negative decision under the conservative work-growth cap.

Retain **16×7**. The original-heap 14-layout search wins decisively at that
point. With wide chunks, 8×14 trends 3.8% faster on training but fails ≥5%
on both sources; 8×7 is 1.75–1.80× slower. No evidence supports reducing
active cores for elapsed-time performance here. Requested partial chunks
are not selected. Wider full chunks restore millions of peer transfers
and retain 64-item amortization; they do not reduce edge work or rounds.

Exact opt-in settings/binary: `benchmarks/delta-road-selected.json`.
Full result: current-state §19 and `onenode-data/delta-road-time-22401351.json`.
The source implementation, experiments and results are committed stepwise.
Stop the bounded study. The ordinary/distributed default is unchanged;
no new global gate or distributed performance claim follows from this test.


### Delta RMAT one-node comparison (2026-09-25)

The user requests an RMAT run after the road study. Use the existing
`rmat25` input (33,554,432 vertices, 1,047,199,108 stored directed edges,
maximum weight 1000) and its independent full reference for two training
and four test sources. One exclusive `cpu-interactive` node, 16×7,
`+old-scheduler`, same frozen September 25 runtime. No layout sweep or
new solver change is part of this comparison.

Compare original ACIC, the latest road-study binary and a duplicate
original control against Wasp. RMAT's automatic gate disables process
sharing/chunks; lazy relaxation and degree 256 hub hints remain enabled.
The shared heap slice is inactive. Verify those modes and every PE's
affinity; do not interpret timing noise as a chunk optimization gain.
Use no road-specific admission-width override.

Wasp gets a bounded 32/64/96/128-thread × delta 1/4/16/64/256 search on the
two training sources, then repeats the two fastest settings on both.
Freeze before the held-out comparison. Each arm/test source gets a
warmup and three randomized timed launches, for 112 full solves including
training and smoke checks. Full digest mismatch, wrong modes or affinity,
truncation/rescue or launch failure aborts the job. The non-lazy edge-work
ledger formula is inapplicable to lazy tokens and is deliberately unused.
This is a one-node ACIC/Wasp result, not a distributed scaling claim.

Protocol: `benchmarks/delta-rmat25-wasp-protocol.json`. Campaign:
`/work/hdd/mzu/rao1/acic-rmat25-wasp-20260925`. All binaries, harness code,
commands, runtime/graph hashes and individual solves are archived.

RMAT job **22401925** completed on cn100 in **12:23**. All **112 full solves**
and the raw-log audit pass. Wasp selects 64 threads/delta 1 using training
sources only. Four held-out source medians are current ACIC **0.778–0.845 s**
versus Wasp **0.637–0.648 s**: ACIC speedup **0.754–0.833×**, geometrically
**0.792×**. Current/original ACIC speedup is **0.936–1.074×**, geometrically
0.992×, versus original/control ratios of 0.965–1.046×. The mixed differences
show no systematic gain and do not establish a strict regression-free gate.

**Decision:** retain the existing nonshared lazy/hint path; no solver/default
change or new optimization follows from this comparison. Chunks and shared
heap slice are inactive, so no chunk gain may be claimed. One allocation and
a bounded Wasp search limit the result. No further job is submitted.

Master output: `logs/compare-22401925.out`; raw logs/manifest/selection/summary
and the independent `audit.json`/`report.md`: `logs/compare-22401925/` in the
campaign. `benchmarks/rmat_wasp_report.py` rechecks raw digests, modes,
bindings, times and the training-only selection. See current-state §20 and
`design/onenode-data/delta-rmat25-wasp-22401925.json` for the complete record.
