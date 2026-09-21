# IPDPS27 paper skeleton

*2026-09-18. Ten IEEE double-column pages for body, figures and tables;
references outside. Double-anonymous: cite the IA³@SC24 paper in the third
person, no repository or cluster-allocation names that identify the authors,
artifact links anonymized. Claims refer to [ipdps27-sprint.md](ipdps27-sprint.md) §1.*

**Status, 2026-09-19: blocked as a performance-led submission.** The completed
Delta candidate improves greatly over frozen ACIC but remains 2.5–4.1 times
slower at eight nodes than tuned one-node GAPBS on the high-diameter targets.
Its full acceptance protocol is incomplete. C6 has not passed; L3 has not
established a live-feedback contribution. This outline is conditional, not a
claim that the current results support submission. See the
[results](ipdps27-onenode-results-22222842.md) and
[revised plan](ipdps27-onenode-gap.md).

**2026-09-21 direction under investigation:** use established local execution
techniques where they help, credit them, and judge a narrower high-diameter
paper by its distributed contribution. The authorized
[queue-batching experiment](ipdps27-r1-batch-decision.md) tests local cost;
it is not itself a novelty or scaling result. A candidate claim is that ACIC's
distributed control preserves useful parallelism while limiting redundant
work as node count grows. Establish that with an optimized one-node baseline,
fixed-size scaling, larger-graph capacity experiments and distributed
competitors, including an ablation of the claimed control mechanism. Show
absolute times as well as speedup. Current slack-off results cannot support
a live-feedback claim. The current submission gate is unchanged.

The [completed local batching experiment](ipdps27-r1-batch-one-node-results.md)
supports that infrastructure choice: all 576 solves validate, and batch 8
reduces one-node time 60–61% on mesh and 33–35% on road against frozen R0.
It remains 2.86–2.87 times slower than matched GAPBS on mesh and 3.04–3.85
times on road. Batch 32 brings the mesh gap to 2.41–2.47 times but increases
road work substantially. Distributed scaling of the batched build is not
yet measured; the old slower one-node baseline cannot support a speedup claim
for this optimized candidate.

**Provisional working title:** *Adaptive Message Flow for Asynchronous SSSP on
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
| 6 | Results | 3.5 | 6.1 Best feasible baseline: one-node GAPBS alongside ACIC 1/2/8 nodes and distributed RIKEN/Gluon (Fig. 1 + Table 2). 6.2 Causal ablation (Fig. 2). 6.3 Live feedback vs strong fixed policy, only if established (Table 3). 6.4 Work per reachable arc and CPU cost per attempt, with placement/queue/control attribution (Fig. 3). 6.5 Scale-free losses and limits of the explanation (Fig. 4) | C1–C6 | Final-build R0/R3; earlier E1/E3 labeled by version |
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
3. A result relative to the best feasible baseline, including one-node
   GAPBS when the input fits there. Earlier builds' distributed wins and
   1.9–3.7× scale-free losses to RIKEN remain historical evidence; the final
   candidate needs its own frozen comparison. No practical high-diameter
   performance advantage over GAPBS has been established.
4. Adaptive message flow in the sense of sprint doc §1b. Flow control
   driven by runtime events (C3a), and decisions made when the graph is
   read that reach per-graph tuned performance (C3b), each mapped to the
   Charm++ abstraction it relies on. Live parameter feedback (C3c) is
   claimed only if it beats the same mechanism with a strong fixed parameter
   in independent allocations. Implemented L3 does not currently qualify.
5. A new mechanism explaining and improving the tradeoff between work,
   placement and per-operation cost, if R0/R1 establish it. Tiling and shared
   state are implemented; their combination has not closed C6. Passing C6 is
   a submission prerequisite, not by itself a novel contribution.

If the performance route stops, do not simply rename this outline a robustness
or characterization paper. Such a paper needs a new, falsifiable claim,
validation on unseen cases, and a clear advance over the workshop and existing
asynchronous priority/work-sharing designs. Runtime overhead is a hypothesis
to separate from redundant work, not an established fundamental limit.

## Figures and tables

| # | What | Source |
|---|---|---|
| Table 1 | Inputs: V, stored arcs, average degree, max distance (per source range), class | `*.meta`, `*.reference.txt` |
| Fig. 1 | Solve time, ACIC at 1/2/8 nodes vs RIKEN, Gluon-Async and tuned one-node GAPBS, log scale, all graphs; show source variation and both allocations separately | Final-build R3; completed candidate report until then |
| Table 2 | Paired medians and allocation floors, per graph and node count; failures and hangs column | report_arms.py over 8g + E3 |
| Fig. 2 | Ablation: slowdown of each one-axis arm and `ws24`/`ws24-wide` vs `current`, per graph, 8 nodes (2 nodes in text) | E1 |
| Table 3 | `current` vs `global-fixed` and `tuned-fixed` (chosen setting named), 2 and 8 nodes | E1 |
| Fig. 3 | High-diameter scaling, edge attempts per reachable arc, CPU cost per attempt, queue/delivery samples and exposed controller latency; do not add overlapping timers | R0 on the exact candidate |
| Fig. 4 | Scale-free: ACIC delivered updates per edge vs nodes against RIKEN's relaxations per edge | 8a |

## Threats a reviewer will raise, and the planned answer

| Threat | Answer in the paper |
|---|---|
| "One-node GAPBS beats all of this" | Currently true on the measured high-diameter targets. The draft is blocked under C6; show this comparison first, and only revise the conclusion when the full gate passes |
| "Easy in Charm++, hard in MPI" is asserted, not shown | The mechanism-to-API table describes implementation, not comparative effort. Omit the stronger claim unless an equivalent implementation or measured engineering comparison supports it |
| "The 2024 paper was already adaptive" | 2024 controlled two admission percentiles, and its buffer size was hand-picked per node count. This paper controls the aggregation layer, flush timing, relaxation order and placement, from the graph as read and from live flow |
| "The 2024 paper beat RIKEN on uniform graphs; this one doesn't" | RIKEN is now searched over layout and delta; state the reversal |
| "RIKEN is mistuned" | E3's grid with interior optima, named in Table 2; RIKEN runs on road-usa-w4 |
| "Road-usa has no RIKEN cell" | The weight-scaled road supplies a comparable integer-weight input, but earlier RIKEN held-out correctness failures must remain visible and be diagnosed; invalid cells cannot support a speedup claim |
| "Gains are implementation, not ideas" | `ws24-wide` vs `ws24` vs `current` separates wire format from mechanisms |
| "Adaptive = tuned per graph in disguise" | Table 3, and the regime rules stated as degree thresholds, not as adaptation |
| "Only 8 nodes" | Stated as a limitation; larger runs require a separately justified research hypothesis and do not repair the failed one-node comparison |
| "Workshop paper overlap" | §2 of the sprint doc, checked against the PDF |
