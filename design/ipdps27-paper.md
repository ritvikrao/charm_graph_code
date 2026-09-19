# IPDPS27 paper skeleton

*2026-09-18. Ten IEEE double-column pages for body, figures and tables;
references outside. Double-anonymous: cite the IA³@SC24 paper in the third
person, no repository or cluster-allocation names that identify the authors,
artifact links anonymized. Claims refer to [ipdps27-sprint.md](ipdps27-sprint.md) §1.*

**Working title:** *Adaptive Message Flow for Asynchronous SSSP on
High-Diameter Graphs*. "Adaptive" is used in the author's sense (sprint doc
§1b): real-time control of message flow, from the graph as read and from
live flow, built on Charm++ abstractions. It must stay distinct from the
2024 title ("An Adaptive Asynchronous Approach for the Single-Source
Shortest Paths Problem"). The paper is submitted only if C6 passes
([ipdps27-onenode-gap.md](ipdps27-onenode-gap.md)).

## Page budget

| § | Section | Pages | Content | Claims | Data |
|---|---|---:|---|---|---|
| 1 | Introduction | 1.0 | High-diameter graphs make frontier-synchronous SSSP pay one global step per distance band; asynchronous alternatives pay in wasted work. Contribution bullets (below). The one-sentence result, including the losses | C1, C2, C5, C6 | 8g, E1 |
| 2 | Background and related work | 1.0 | Δ-stepping and its distributed forms (RIKEN / Graph500 SSSP, SC22 study as a scale reference), Gluon-Async, GAPBS, Wasp, GBBS/Julienne; GPU SSSP (Gunrock, cuGraph, G2, Atos) as scope; the IA³@SC24 predecessor in the third person, and what it lacked | — | — |
| 3 | Design | 2.0 | Solver loop and histogram controller (short, it is prior work); then the new mechanisms, one paragraph each with its rule and why: lazy heavy relaxation, delivery cadence and idle flush (with interval), coarsening, buffer size by degree, send filter, delivery skipping. Correctness: progress rescue and overflow ordering (a repair, stated as such) | C2 | step 7–8 docs |
| 4 | Implementation | 0.75 | Charm++ on Reconverse, htram aggregation, compact wire items, hold bitmap, process layout (8×15 / 16×7), what is compile-time and what is runtime | — | — |
| 5 | Methodology | 0.75 | Inputs (Table 1: V, E, degree, diameter proxy, max distance; canonical undirected min-weight; not Graph500), sources (seeded, held-out), validation (digests, independent validator), timing boundaries per system, layout and parameter search per system with its grid, floors and repetition, hardware | C7 | §4 of sprint doc |
| 6 | Results | 3.5 | 6.1 Distributed comparison at 2 and 8 nodes (Fig. 1 + Table 2). 6.2 Where the gain comes from: ablation (Fig. 2). 6.3 Adaptive vs fixed (Table 3). 6.4 Scaling and where time goes (Fig. 3: 1/2/8 nodes; round counts, comm shares). 6.5 Scale-free graphs and one-node GAPBS: the losses and their causes (Fig. 4: work per edge vs nodes) | C1–C6 | 8g, E1, E2, E3 |
| 7 | Discussion and limitations | 0.5 | When to use it; COST-style ratio against one-node GAPBS; graphs that fit one node; no runs above 8 nodes; integer weights; 32-bit wire limit | C4–C6 | — |
| 8 | Conclusion | 0.25 | | | |
| | Figures and tables inside the above | — | ~2.25 pages equivalent | | |

## Contribution bullets (draft, each must survive E1/E3)

1. The mechanisms added since the workshop design, each defined by a rule that
   depends only on graph degree or runtime state, and measured one at a time
   on a single binary (E1).
2. An evaluation against distributed Δ-stepping (RIKEN), asynchronous bulk
   SSSP (Gluon-Async) and a one-node reference (GAPBS), with each system
   choosing its own layout and parameters, on independently validated inputs.
3. The result: 7–79× over the best distributed baseline on high-diameter
   graphs at 8 nodes, and a stated 1.9–3.7× loss to RIKEN on scale-free
   graphs with its cause (work per edge growing with node count; 8a).
4. Adaptive message flow in the sense of sprint doc §1b. Flow control
   driven by runtime events (C3a), and decisions made when the graph is
   read that reach per-graph tuned performance (C3b), each mapped to the
   Charm++ abstraction it relies on. Live parameter feedback (C3c) is
   claimed only if L3 clears its A/B.
5. Closing the one-node gap (C6): tiled placement, shared state within a
   process, and live control of how far a PE runs ahead, measured one at a
   time. The contribution is claimed only for the levers that are adopted.

## Figures and tables

| # | What | Source |
|---|---|---|
| Table 1 | Inputs: V, stored arcs, average degree, max distance (per source range), class | `*.meta`, `*.reference.txt` |
| Fig. 1 | Solve time, ACIC vs RIKEN vs Gluon-Async vs one-node GAPBS line, 2 and 8 nodes, log scale, all graphs; error bars = min/max over sources × reps | 8g + E3 |
| Table 2 | Paired medians and allocation floors, per graph and node count; failures and hangs column | report_arms.py over 8g + E3 |
| Fig. 2 | Ablation: slowdown of each one-axis arm and `ws24`/`ws24-wide` vs `current`, per graph, 8 nodes (2 nodes in text) | E1 |
| Table 3 | `current` vs `global-fixed` and `tuned-fixed` (chosen setting named), 2 and 8 nodes | E1 |
| Fig. 3 | High-diameter strong scaling 1/2/8 nodes; rounds and time per round; compute/send/idle shares | E2 |
| Fig. 4 | Scale-free: ACIC delivered updates per edge vs nodes against RIKEN's relaxations per edge | 8a |

## Threats a reviewer will raise, and the planned answer

| Threat | Answer in the paper |
|---|---|
| "One-node GAPBS beats all of this" | Must be false before submission (C6 is a go/no-go condition). Fig. 1 carries the one-node GAPBS line, and every table has a one-node ACIC column |
| "Easy in Charm++, hard in MPI" is asserted, not shown | Design section table: mechanism → Charm++ feature → what an MPI code would need (sprint doc §1b); each mechanism's measured effect next to it |
| "The 2024 paper was already adaptive" | 2024 controlled two admission percentiles, and its buffer size was hand-picked per node count. This paper controls the aggregation layer, flush timing, relaxation order and placement, from the graph as read and from live flow |
| "The 2024 paper beat RIKEN on uniform graphs; this one doesn't" | RIKEN is now searched over layout and delta; state the reversal |
| "RIKEN is mistuned" | E3's grid with interior optima, named in Table 2; RIKEN runs on road-usa-w4 |
| "Road-usa has no RIKEN cell" | road-usa-w4 (same topology, weights ÷ 4, exact in binary32) |
| "Gains are implementation, not ideas" | `ws24-wide` vs `ws24` vs `current` separates wire format from mechanisms |
| "Adaptive = tuned per graph in disguise" | Table 3, and the regime rules stated as degree thresholds, not as adaptation |
| "Only 8 nodes" | Stated as a limitation; the SC27 plan carries the larger runs |
| "Workshop paper overlap" | §2 of the sprint doc, checked against the PDF |
