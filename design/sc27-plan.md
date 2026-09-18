# ACIC → IPDPS27 / SC27: research and engineering plan

*Drafted 2026-09-10; revised after step 7 (09-13), compacted with the Gate A
checkpoints (09-15), re-evaluated after 7.6j, updated with 7.6k–o and step 8 added (09-16); IPDPS27 decision added (09-18). `htram_group.*`
references are in github.com/UIUC-PPL/htram.*

## Status

Steps 1–7 (SSSP development) are complete. The [7.5 comparison
pilot](step75-comparisons.md) is complete but **cannot be quoted**: 7.6c found
that process count dominates every other deployment axis by 7–20× (one node,
120 workers: eight processes of fifteen beat one of 120 by 7.4× on mesh22, 20.6×
on rmat22, 15.5× on road-ny), and the pilot ran ACIC at one process per node
while RIKEN tuned its rank count. The same confound reversed 7.6d's width table
on all nine graphs. Every result before 7.6c is superseded, not merged.

Since then: 7.6a–c closed, 7.6d settled as a mesh-only rule, 7.6e rejected, the
default deadlock found and repaired (7.6g), hang rate and resolution floor in
every table (7.6h), and two waves of the admission × delivery study (7.6f2).
**Checkpoints 1 and 2 read negative (09-15).** On large inputs the benefit is
not co-designed, adaptation loses to a fixed width on road-usa by 8–10×, and no
graph but mesh26 gets faster past two nodes ([below](#76f2-so-far)). The
external re-take at fair layouts ([7.6f1](step76-external.md)) then found:

- one-node GAPBS faster than ACIC on every graph: 2–21× against ACIC on one
  node, 1.1–21× against ACIC on two;
- RIKEN 6–10× faster on rmat25 and orkut, and 18–21× slower on mesh24;
- ACIC 3–10× faster than Gluon-Async on mesh24 and road-usa, 1.25–2.6× on
  orkut, and split with it on rmat25.

That met this plan's "stop the SC27 plan" condition ([outcome
reading](#gate-a-checkpoints)). Rationale and protocol are in
[post-step7-review.md](post-step7-review.md).

**Re-evaluation after 7.6j (09-16): the stop decision is deferred, not
taken.** Much of the 7.6f1 gap turned out to be ACIC's own send path, not
its algorithm ([perf](step76-perf.md)). Sending fewer bytes took rmat25 at
two nodes from 300 to 121 ns per graph edge (RIKEN 49, GAPBS 145) and orkut
from 0.65 s to 0.22 s; the 7.6f1 ACIC column is therefore stale. The
remaining argument for continuing is a scaling one: once ACIC's per-edge
cost is close enough to RIKEN's that communication dominates at larger node
counts, ACIC's asynchronous, aggregated delivery is where it can win. So the
order is now:

1. **Close more of the per-edge gap at two nodes** (7.6k-m below), with no
   runs above two nodes until the [scaling entry
   condition](#scaling-entry-condition) holds.
2. **Re-take 7.6f1 at one and two nodes** with the new build (7.6n).
3. **Then a bounded scaling comparison** against RIKEN and Gluon-Async
   (7.6o), which becomes the Gate A evidence.

**7.6k–n (09-16): the entry condition is missed, and the stop decision is
open again.** Shared memory between processes, a controller-chosen buffer
size and a cheaper destination lookup made ACIC 1.3–2.8× faster than in
7.6f1 on every graph and node count, and ACIC now beats Gluon-Async
everywhere. But RIKEN is still 2.8× faster on rmat25 and 3.5× on orkut at two
nodes, against the roughly 2× the [entry
condition](#scaling-entry-condition) asks for, and one-node GAPBS is still
faster on every graph. The rmat25 gap narrows from one node to two (4.3× to
2.8×), which is the direction the scaling argument needs; the orkut gap
widens (2.4× to 3.5×). Options: stop as the plan says; spend one more bounded
step on orkut's buffer size and a profile of the shared-memory build; or run
a small 7.6o on rmat25 alone to test whether the narrowing continues.

**7.6o (09-16): scaling runs the wrong way; step 8 added instead of
stopping.** At 8 nodes ACIC is *slower* than at 2 (rmat25 0.64 → 0.80 s,
orkut 0.23 → 0.32 s) while RIKEN gets 1.6–3× faster, so RIKEN's lead grows
to 11.7× and 7.5× ([7.6o](step76-external.md#76o-scaling-comparison)). All
three systems spend about half their time communicating at 8 nodes, so the
scaling premise holds; what fails is ACIC's work per edge, which grows 3.8×
from 2 to 8 nodes (1.2 → 4.6 delivered updates per edge on rmat25), with 34×
the idle flushes and less than half the controller rounds. RIKEN's work was
not counted, but its design prunes it (light/heavy phases, shared settled
bitmaps). The 16-node run was not
submitted. [Step 8](step8-scaling.md) attributes that growth and targets it
(buffering by fan-out, a controller whose cost does not grow with PEs,
light/heavy pruning, runtime overhead) before 7.6o is re-taken as 8g, with
its own stop rule.

**Step 8 and 8g (09-18): scaling fixed on scale-free graphs, lead not
closed.** Lazy heavy relaxation (8d), the htram hold bitmap and the idle-flush
interval (8e), and skipping empty deliveries on scale-free graphs (8b) take
ACIC's 8-node times from 0.80 to 0.37 s on rmat25 and 0.32 to 0.14 s on orkut.
ACIC now gets faster from 2 to 8 nodes on all four scale-free graphs (1.1–2.3×).
RIKEN still leads at 8 nodes by 3.65× (rmat25), 1.87× (orkut), 3.54× (rmat26)
and 3.47× (rmat27), and the lead still grows from 2 to 8 nodes on the RMAT
graphs. The 16-node entry condition (at most 3× on rmat25 and orkut) is missed
on rmat25, and the stop rule (more than 5×) is not triggered. On high-diameter
graphs ACIC is 19–53× ahead of RIKEN and 7–79× ahead of Gluon-Async at 8 nodes,
but its own time is flat from 2 to 8 nodes. Details and the Gate A reading are
in [8g](step8-scaling.md#8g-results-the-re-take-at-2-and-8-nodes-2026-09-18).

**Decision on 09-18:** pursue a bounded, SSSP-only IPDPS27 submission test.
The 8g results justify a high-diameter performance claim against distributed
CPU baselines, but do not yet justify a general adaptivity/co-design or strong
scaling claim. ACIC's mesh24 and road-usa solve times are almost flat from two
to eight nodes. Read the [IPDPS sprint](#ipdps27-submission-sprint-2026-09-18-to-10-08)
before adding another optimization step. Gate A for the broader SC27 program
remains open; its October 18 date cannot decide an October 8 submission.

Checkpoint 1's "co-design: no" still stands for admission and delivery
thresholds. A controller-chosen buffer size (7.6k) was a new, direct test of
the same claim. It found the right regime on all four graphs, but from the
graph's degree; the algorithm's feedback never had to correct it, so it does
not show co-design either.

## IPDPS27 submission sprint (2026-09-18 to 10-08)

The [official CFP](https://www.ipdps.org/ipdps2027/2027-call-for-papers.html)
requires a registered abstract (at most 500 words) by **October 1 AOE** and a
full, double-anonymous manuscript by **October 8 AOE**, with a firm deadline.
The submission limit is ten IEEE double-column pages for body, figures and
tables; references are outside the limit. A reproducibility appendix is
required for accepted papers, not an extra submission appendix. Review the
CFP's rule against previously published manuscripts and its anonymity rules
when explaining the IA³@SC24 workshop predecessor and preparing artifact
links. The submitted contribution must be substantially new. Do not assume a January
revision can supply evidence missing from the October submission.

**Provisional thesis:** ACIC's bounded asynchronous admission and aggregated
delivery make distributed weighted SSSP fast on sparse, high-diameter graphs
where repeated global frontiers are expensive. The contribution since IA³@SC24
must be identified as specific new mechanisms and their causal effects, not
the already published ACIC idea, correctness repairs, or implementation speedups
alone. A possible title is *Adaptive Communication for High-Diameter SSSP on
CPU Clusters* **only if** the new-build fixed-policy ablation proves adaptation
matters. Otherwise pitch the measured scheduling and communication mechanism,
with a title that does not promise adaptivity. Include the scale-free losses and
one-node GAPBS result prominently. RIKEN has no comparable road-usa result with
its current binary32 distances, so do not call that cell an ACIC win over RIKEN.

**What is currently supported:** at eight nodes, ACIC beats RIKEN 19× on
mesh24 and 53× on mesh26, and beats Gluon-Async 8.1×, 7.4× and 79× on mesh24,
mesh26 and road-usa respectively. It trails RIKEN 1.87–3.65× on the four
scale-free graphs in the 8g allocation. Those ratios are solve-only, at four
held-out sources, and the 8g allocation was slow for some systems; they are
not yet final paper figures. ACIC is also slower than one-node GAPBS on every
graph in 7.6n. The 7.6f2 large-input ablations rejected the joint-feedback
co-design claim and showed a severe road-usa loss to tuned fixed admission
under the old width/default configuration. Re-run the relevant ablations on
the 8g build before calling the current default adaptive.

**Finite work budget and stop rule.** Freeze the candidate algorithm after at
most two diagnostic, hypothesis-driven changes, each with a correctness check
and paired A/B against the 8g build. Do not chase RIKEN on RMAT for this
submission: a 1.9–3.7× remaining gap, noisy 8g rmat25/orkut ratios, and the
October deadline make that a separate research track. Reserve at least the
last seven days for analysis, figures and writing. Stop optimizing earlier if a
change fails to clear its allocation floor in two allocations, moves the cost
to another graph/source, or creates a validation/hang regression. No BFS,
PageRank, generic payload refactor, or speculative 16-node sweep before the
submission decision.

| Date | Deliverable / decision |
|---|---|
| Sep 18–21 | Draft one-page claim/evidence table and 10-page skeleton. Pin the 8g source, binary and baseline versions. Check 2024 overlap. Run independent cross-system distance validation on paper graphs and sources; verify graph direction, weights, duplicates and timing boundaries. Prepare at least one high-diameter real graph/weight variant that RIKEN can represent, if feasible. |
| Sep 22–25 | On the final build, compare the repaired 2024-style ACIC, current default, fixed thresholds with progress/termination preserved, graph-tuned fixed, and one-axis ablations on high-diameter and scale-free graphs. Use held-out sources and repeated allocations. Measure solve, setup, work, bytes, message counts, peak memory and failures. Test only one or two diagnosed changes. |
| **Sep 26 go/no-go** | Submit to IPDPS only if a new post-workshop mechanism has a causal, reproducible time-to-solution gain; high-diameter wins survive independent validation and fair baseline layouts; and the 10-page paper can explain the scale-free/GAPBS losses. If adaptive and strong fixed policies tie, remove the adaptivity claim. If the only advantage is against a poorly matched baseline or the result depends on the old width bug, keep the work for SC27. |
| Sep 27–30 | Freeze data, produce figures/tables with uncertainty and failures, write a complete anonymized draft, get a skeptical internal read, and register a truthful ≤500-word abstract by Oct 1 AOE if the go gate passed. |
| Oct 1–7 | Revise, audit every result against raw logs and source version, verify citations and anonymity, and submit the full paper by Oct 8 AOE. |

GPU SSSP belongs in related work and scope, with a bounded direct comparison
only if matching GPU hardware and input semantics are available without
displacing the CPU causal study. Single- and multi-GPU weighted SSSP exist:
[Gunrock](https://github.com/gunrock/gunrock),
[cuGraph](https://docs.rapids.ai/api/cugraph/stable/api_docs/api/plc/pylibcugraph.sssp/),
and [G2/GraphIt GPU](https://jshun.csail.mit.edu/gpu-graphit.pdf) are relevant;
[Atos](https://escholarship.org/uc/item/9f17k8gk) is a task-parallel GPU
counterexample to a claim that asynchrony itself is unique to Charm++.
[Recent GPU queue work](https://arxiv.org/abs/2602.10080) also studies
input-adaptive SSSP scheduling. State hardware, memory capacity, graph fit,
precision/weights, and whether transfer/build time is included before any
CPU/GPU runtime claim. A CPU-cluster contribution does not require beating a
GPU, but omitting these systems would make a broad "fastest SSSP" pitch
indefensible.

## Context

The IA³@SC24 paper (`~/Downloads/acic_2024paper.pdf`) introduced **ACIC**
(Asynchronous Continuous Introspection and Control): asynchronous distributed
SSSP in Charm++ (`sssp_smp.cpp`) over the htram aggregation library. A continuous
cycle of reductions builds a global histogram of active updates by tentative
distance; percentile thresholds from it gate which updates enter a PE's priority
queue (`heap_threshold`) and which are sent (`tram_threshold`).

The 2024 paper's gaps: RIKEN Δ-stepping was 2.8–3.3× faster on RMAT (a 2024
number, not a current comparison); one algorithm, two synthetic graph types,
16 nodes, no weighted reader, no validation; and an "adaptive aggregation
library" with no self-tuning. Steps 1–7 closed the engineering and validation
gaps and added adaptive policies. Open: performance on large and real inputs,
external competitiveness, and adaptation against strong fixed settings.

**Target:** SC27 full paper, **early April 2027 provisionally** (SC26: abstracts
Apr 1, papers Apr 8, AD Apr 28; [schedule](https://sc26.supercomputing.org/all-dates-deadlines/)).
Re-anchor when the SC27 CFP appears.

**Claim to defend (hypothesis):** *adaptive introspection co-designed across the
algorithm and the communication layer*: one feedback signal steers both work
admission and the library's buffering/flushing, across a stated class of graph
algorithms and structures. Test shared feedback against independent policies
and strong fixed configurations.

**Report time to solution and work together.** Work reduction explains a
result; it is not a runtime or energy benefit. Count edge examinations,
distance changes, messages, memory and control overhead separately.

### Defects found in planning (fixed; provenance)

Three findings changed how to read the 2024 results; all are fixed and the
original predictions are superseded by the step notes:

1. `tflush()` never decremented `updates_in_tram`, arming a 2048-iteration scan
   per edge. The fix did not reproduce the paper's `p_tram` conclusion; the
   laptop sweep was nearly flat ([defect-fixes.md](defect-fixes.md#the-p_tram-sweep-re-run)).
2. `HTramMessage` was not varsize. The predicted wire padding did not occur,
   because buffer and message size always moved together
   ([varsize-messages.md](varsize-messages.md) §2).
3. `tram_hold` over-allocated by `CkNumPes()` rows, plus eight smaller bugs
   (heap overflow in `dest_table`, `histogram[-1]`, `NODE_COUNT 512`, `rand()`
   flush gate, silent 30 s truncation, PE/chare identity, a dead atomic, repeated
   `get_dest_proc`), recorded in [defect-fixes.md](defect-fixes.md) and
   [verify-harness.md](verify-harness.md). Dead code went in step 5
   ([graphlib.md](graphlib.md) §1).

## Strategy

- **Establish the SSSP contribution, then test transfer.** 7.5–7.6 have a
  bounded budget; the external comparison and fixed-policy references decide
  between a targeted optimization, a second kernel, or a narrower claim.
- **Min-relaxation and additive-residual algorithms need distinct contracts.**
  SSSP/BFS exploit monotonicity and idempotence; PageRank must conserve buffered
  residual mass and validate to a tolerance. No universal `(key, value, combine)`
  promise.
- **Type erasure only when a second payload needs it:** a byte-oriented
  `HTramCore`, a header-only `HTram<T>` façade, a POD `HTramOps` vtable, and a
  `BuiltinOp` enum switched outside the insert loop. Templating the whole class
  kills the static archive and forces `.ci` instantiation per application.
  Possible later layout: `htram/` (core, ops, hold, combine), `acic/`
  (controller), `graphlib/` (types, partition, CSR builder), `engine/`
  (update engine, per-algorithm headers).
- **Runtime controls:** keep the working Delta and Anvil launch configurations.
  Record worker/rank layout, affinity, compiler/runtime revisions,
  `+lci_ndevices`, `LCI_ATTR_PACKET_SIZE`, the Charm++ shared-memory setting
  (`--enable-shmem`, `++ipcpoolsize`), the wire item format (`WIRE`), buffer
  size and `--send-filter-bits`. Fix transport settings for
  comparisons and run a small sensitivity check before crediting a buffer-size
  optimum to adaptation. Profiles locate cost; traces show exposed delay;
  idleness alone does not identify a bottleneck.

## Work plan

Research gates produce decision reports; implementation gates require
correctness and performance checks. Integer-distance refactors must keep
identical results on one and two nodes; PageRank uses a tolerance gate.

| # | Step | Result or gate | Status |
|---|---|---|---|
| 1–5 | Runtime port; deterministic graphs and Dijkstra digests; control-path and memory repairs; varsize messages; dead-code retirement, graphlib, flat CSR, binary inputs | [Verify harness](verify-harness.md), [defect fixes](defect-fixes.md), [messages](varsize-messages.md), [graphlib](graphlib.md); 18 verification configurations | complete |
| 6 | H1–H4 diagnosis on recorded synthetic configurations | [Diagnosis](scale-free-diagnosis.md); qualified by step 7 | complete |
| 7 | Gated flush cadence, combining/fold, bucket coarsening, idle flush | Mesh cadence 3.6–3.8×; combining off; two-node RMAT idle flush 1.12× with 12.6% fewer updates ([evidence](post-step7-review.md#what-the-evidence-supports)) | complete |
| 7.5 | Matched inputs, digest checks, fixed-policy tuning, RIKEN/GAPBS/Gluon, road/social inputs, 1–16 nodes | [Pilot](step75-comparisons.md); superseded by the geometry confound | complete, not quotable |
| 7.6a | Progress repair: count the source update so the empty-window rescue fires | [Progress](step76-progress.md); RMAT failures gated | complete |
| 7.6b | Controller robustness: width, two-tier rule, clamping, geometry frozen | [Controller](step76-controller.md) | complete |
| 7.6c | Deployment variables one at a time (one node) | [Deployment](step76-deployment.md): **process count 7–20×**; LCI's own shared memory (small messages only) 2× slower | complete |
| 7.6d | Initial width, `log(V)` vs heaviest edge | [Width](step76-width-repair.md): mesh20/mesh22 win every allocation (83/84 paired runs); others regress somewhere. Ships as `PER_GRAPH_WIDTH_RULE` | closed |
| 7.6e | Raise the clamp mid-run for out-of-range graphs | [Range](step76-range.md): 35/36 hang on the meshes at 8–16 nodes; off by default | rejected |
| 7.6g | The default deadlocks | [Deadlock](step76-default-deadlock.md): a flagged heap top hides admissible updates after coarsening; `--pq-overflow-last` (default on) takes mesh22 from 12/80 hangs to 0/80. Skew hypothesis refuted; `verify.sh` stall checks and readonly flags fixed; gate fixture fails without the repair | closed |
| 7.6h | Hang rate and resolution floor beside every speedup | `outcomes.py` (ok/hung/wrong/crashed, rescues counted, per-allocation floors), `report_arms.py`, `report.py` | complete |
| **7.6f1** | External re-take, every system tuning its own layout (`configs()` needs an ACIC ranks-per-node axis) | [External](step76-external.md): GAPBS (1 node) faster everywhere (1.1–21×); RIKEN 6–10× faster on scale-free, 18–21× slower on mesh24; ACIC beats Gluon-Async 3–10× on high-diameter | **complete (1–2 nodes): external systems well ahead** |
| **7.6f2** | Admission × delivery vs `global-fixed` and per-graph `tuned-fixed` | [Policy](step76-policy.md), below. Stopping rule: one or two supported optimizations | **checkpoint 1 read: negative** |
| 7.6i | Why 7.6f1 lost: Projections traces of rmat25 at 2 nodes | [Traces](step76-traces.md): 67–69% in `process_heap`, idle 10–14% (ramp and drain), TRAM paths 14–15%, balanced to 1.03×. 318 ns per graph edge against RIKEN's 49 and GAPBS's 145; 98.8% of delivered updates rejected | complete |
| 7.6j | Per-edge cost: counters, then the send path | [Perf](step76-perf.md): bound by bytes on the wire, not CPU (45% of PE time spinning in LCI sends). Compact 8-byte items, larger buffers, a sender-side dominance filter and prefetch: rmat25 300 → 121 ns/edge (2.5×), orkut 2.9×; high-diameter graphs want a 1024-item buffer size (1.2–1.4×) and lose 2× at 6144 | complete |
| 7.6k | Buffer size chosen by the controller; send filter by regime | [KLM](step76-klm.md): starts at 256 items per unit of average degree, corrected by the share of arrivals that improve a distance; filter on only at 2048+ items. Now the default. Within the floor of the best fixed size on rmat25 and mesh24, 1.53× faster than it on road-usa (512 items), 1.13× behind it on orkut (degree puts orkut at 6144, it wants 2048). A regime rule; the feedback correction was never needed | complete |
| 7.6l | Charm++ `--enable-shmem` build of the Reconverse tree | [KLM](step76-klm.md#76l-shared-memory-between-processes): rmat25 1.34× with a much tighter spread, road-usa 1.06×, mesh24 1.00×; pool size irrelevant. The build now used for ACIC runs | complete |
| 7.6m | Next CPU hot spots, then the send spin | [KLM](step76-klm.md#76m-cpu): a one-load destination lookup (1.27× on rmat25) kept; write-prefetch, LTO, `-march=znver3` and LCI backlog sends rejected. rmat25 at 110 ns per edge with the best fixed setting, 116 with the defaults | complete |
| **7.6n** | Re-take 7.6f1 at 1–2 nodes with the 7.6k–m build | [External](step76-external.md#76n-re-take-after-76k-m): ACIC 1.3–2.8× faster than in 7.6f1 everywhere and ahead of Gluon-Async everywhere; RIKEN still 2.8× (rmat25) and 3.5× (orkut) ahead at 2 nodes; GAPBS (1 node) still ahead on every graph | **complete: scaling entry condition missed** |
| **7.6o** | Bounded scaling comparison, scale-free graphs first (rmat25–27, orkut), ACIC vs RIKEN and Gluon-Async, with per-system communication shares | [External](step76-external.md#76o-scaling-comparison): at 8 nodes ACIC slower than at 2, RIKEN 1.6–3× faster; RIKEN lead 2.8→11.7× (rmat25), 3.5→7.5× (orkut), 2.9→5.6× (rmat26), 2.5→4.1× (rmat27); ACIC speeds up only on rmat26/27 (1.07×, 1.44×). ACIC work per edge 3.8× the 2-node value. About half of every system's time is communication | **2 and 8 nodes: negative**; 16 nodes and high-diameter graphs not run; completed by 8g |
| **8** | Make ACIC scale past two nodes: attribute the work growth (8a), buffering by fan-out (8b), controller cost independent of PEs (8c), light/heavy pruning (8d), time outside the solver's work (8e), per-edge CPU if still needed (8f), re-take at 2/8/16 nodes (8g) | [Step 8](step8-scaling.md). Entry to 8g: ACIC faster at 8 nodes than at 2, RIKEN lead ≤ 3× at 8 nodes on rmat25 and orkut. Stop rule: RIKEN > 5× at 8 nodes after 8b–8d | 8a–8g done at 2–8 nodes (09-18); entry to 16 nodes missed on rmat25 (RIKEN 3.65×); **Gate A decision pending** |
| 9 | Generic payload interface, only when a second algorithm needs it | SSSP identical on 1–2 nodes | conditional |
| 10 | PE/chare identity for new mappings; overdecomposition only if profiling justifies it | Correct on all supported mappings | conditional |
| 11 | Extract `AcicController` around observed shared signals | SSSP retained; second kernel uses it | with step 12 |
| 12 | BFS transfer (CC only for a specific hypothesis) | **Gate B:** frozen constants; compare with direction-optimizing BFS | after Gate A |
| 13 | PageRank residual engine, only for a claim beyond traversal | Matched damping, dangling nodes, tolerance | conditional |

### 7.6f2 so far

Jobs 20744389/92/93 and the replication 20744731–34 (1/2/8/16 nodes, 8×15,
4,056 runs, no failures). 44 floor-clearing cells agree in sign across waves and
7 flip, 5 of them because the one-node `global-fixed` selection changed.
Allocation floors ranged 1.09–1.28× at the same node count, so a single
allocation cannot resolve a cell below about 1.3×. Supported at these sizes:

- Adaptive delivery wins on high-diameter graphs and grows with node count
  (road-ny 1.5× at 1 node to 34× at 16; meshes to 4.7–5.8×).
- Coarsening wins on RMAT/youtube and grows with node count (to 1.6–1.8×); on
  uniform it costs at one node and pays from eight.
- `tuned-fixed` beats adaptive only on uniform at low node counts and rmat22 at
  8–16 nodes (1.05–1.59×).
- The two axes compose about multiplicatively, so co-design is **not** shown.

Those inputs were all sub-second. **Large inputs** (jobs 20747230–32; mesh24,
mesh26, rmat25, rmat26 with 67M vertices and 2.1B edges, uniform25, road-usa,
orkut; 845 runs, no failures; 8 and 16 nodes cancelled once they had answered
the checkpoint; [detail](step76-policy.md#large-inputs-checkpoint-1)):

| Question | Answer |
|---|---|
| Survives real work? | Partly. Delivery grows on mesh24 (1.4× to 17× at 16 nodes) and mesh26 (5× at 8); fixed delivery is 1.25× *faster* on road-usa; coarsening does nothing on rmat25/26 at 2 nodes |
| Co-design? | **No.** `local-delivery` ties `adaptive` on mesh24, mesh26 and road-usa at every node count |
| Worth adapting? | **No, as configured.** Beats `global-fixed` 2.5–6.9× on meshes at 8–16 nodes, loses road-usa 7.6–10×, misses the `tuned-fixed` target on road-usa, mesh26 and uniform25 |
| Scales? | **No.** Only mesh26 speeds up (2× from 2 to 8 nodes); road-usa is 6.3 s at 2 nodes and 53 s at 8 |

The road-usa loss is the initial width: adaptive arms bucket at 17 against a
heaviest-edge width for the fixed winners. Delivered updates rise from 2.3B at
2 nodes to 38.6B at 8, against 1.0B for `tuned-fixed`. `PER_GRAPH_WIDTH_RULE`
is keyed by name, so mesh24/26 missed the mesh `weight` rule too; that defect
qualifies the large-mesh rows. Whether `adaptive` at a heaviest-edge width
closes road-usa is cheap to test but waits on 7.6f1.

### 7.6f1 result

Jobs 20750616/17 (1 and 2 nodes, 136 runs, no failures, ~225 SU). Each system
chose its own layout; one toolchain for all (`scripts/anvil/build_baselines.sh`,
`run.py --mode external`). Median solve seconds:

| graph | ACIC 1n / 2n | GAPBS 1n | RIKEN 1n / 2n | Gluon-Async 1n / 2n |
|---|---|---|---|---|
| mesh24 | 1.37 / 0.84 | **0.19** | 26.4 / 17.1 | 4.39 / 3.98 |
| orkut | 0.67 / 0.65 | 0.16 | **0.10 / 0.065** | 0.87 / 1.55 |
| rmat25 | 2.09 / 1.36 | 1.19 | **0.33 / 0.20** | 1.55 / 2.11 |
| road-usa | 3.51 / 3.87 | **0.17** | n/a (binary32) | 27.4 / 37.7 |

Every cell is 4/4 pairs on one side, far above the 1.04–1.06× floors.

- **RIKEN's layout search:** RIKEN chose 16 ranks per node, the largest
  offered, so its times are an upper bound.
- **Width map:** ACIC ran mesh24 and road-usa at the `log(V)` width, but
  `tuned-fixed` would not change a row's direction.
- **Scale:** these inputs fit on one node, and ACIC has not shown it scales
  past two nodes on them.
- **Superseded by 7.6n** for ACIC ([re-take](step76-external.md#76n-re-take-after-76k-m)).
  The ACIC column predates compact items, the send
  filter and the buffer-size finding. On rmat25 at two nodes ACIC is now
  0.53 s (2.6× RIKEN's 0.20 s), and on orkut 0.22 s (3.4× RIKEN's 0.065 s).
  Those two numbers come from a different allocation than the table, so they
  are not quotable until 7.6n.

### Scaling entry condition

No runs above two nodes until all of these hold at one and two nodes, in one
allocation, at each system's chosen layout (7.6n):

- **Scale-free graphs:** ACIC within about 2× of RIKEN per graph edge on
  rmat25 and orkut. The threshold is provisional: the requirement is that at
  larger node counts communication, not ACIC's per-update CPU cost, sets both
  systems' times.
- **High-diameter graphs:** no regression against 7.6f1 on mesh24 or road-usa.
- **Where ACIC's time goes:** a profile showing the share of PE time in
  sends, progress and waiting, so the scaling runs can test whether that
  share grows with node count as expected.

The 7.6o comparison then measures the same shares for RIKEN at 2/8/16 nodes.
ACIC can win only where RIKEN's communication cost outgrows its per-edge
advantage.

7.6o ran anyway, on the user's decision, and answered the share question:
RIKEN spends 46–62% of its 8-node solve in MPI, but its work per edge does
not grow with node count and ACIC's does. For 8g, [step 8's entry
condition](step8-scaling.md#entry-condition-for-8g-and-gate-a) replaces this
one.

## Gate A checkpoints

Decide at fixed points, before more optimization. Cells count only if clean (no
hang, wrong or crashed run) and either above 1.3× in one allocation or the same
sign in two.

**Checkpoint 1, when the large-input wave reports.** First confirm the inputs
are big enough: compute time must clearly exceed setup at 8–16 nodes. If not,
the wave is inconclusive; add rmat27 or mesh28 after checking memory, and do not
read the four questions.

| Question | Continue if | Narrow or stop if |
|---|---|---|
| Does the benefit survive real work? | Adaptive delivery beats fixed on mesh26 and road-usa, and coarsening pays on rmat25/26, at 8–16 nodes | Gains fall within the floor: the small-input growth was a communication-dominated effect |
| Is it co-design? | `adaptive` beats `local-delivery` | They tie: two independent mechanisms, not one shared loop |
| Is adaptation worth it? | Clearly beats `global-fixed`; within ~20% of `tuned-fixed` per case (10% geomean, provisional) | One fixed setting matches: stop refactoring until understood |
| Does it scale? | Time to solution falls from 2 to 16 nodes on the large graphs | 16 nodes slower than 2 on the largest inputs |

A negative answer on the first or third question is not a reason for another
tuning cycle: narrow the claim, or run 7.6f1 before anything else.

**Checkpoint 2, Gate A (by Oct 18).** 7.6f1 read negative at 1–2 nodes; after
7.6j, Gate A requires 7.6n and, if the [entry
condition](#scaling-entry-condition) holds, 7.6o. 7.6o read negative at
8 nodes; Gate A is now read on step 8's re-take (8g). Proceed if ACIC is faster
than RIKEN or Gluon-Async at 8–16 nodes on at least one graph class at a fair
layout, and its advantage grows with node count. Stop if 7.6n misses the entry
condition after 7.6k–m, or if 7.6o shows RIKEN's lead holding at 16 nodes on
every class. Gate A also requires no unexplained stalls. One RMAT ratio alone
does not decide the project, but claims covering scale-free graphs must report
their performance there.

**Checkpoint 3, Gate B (Nov 15).** Frozen constants transfer to BFS, or the
paper is SSSP-only.

**Reading the outcome:**

- **SC27 as planned:** co-design shown, adaptive near per-case tuning on large
  inputs, competitive with Gluon-Async/RIKEN at a fair layout at 16+ nodes (at
  least on high-diameter graphs), and BFS transfer.
- **Narrower paper:** real but independent gains, or competitive only on
  high-diameter graphs. The claim becomes adaptive delivery and admission for
  distributed SSSP; the novelty argument against Gluon-Async gets harder, and a
  smaller venue is more realistic.
- **Stop the SC27 plan:** gains vanish at size, or external systems stay well
  ahead after a fair re-take. Keep the engineering results: the deadlock repair,
  failure and floor reporting, and the process-count finding.

## Negative and qualified results

**Combining** ([step7-combining.md](step7-combining.md)): source hold and batch
fold are correct and off. Neither reduced RMAT relaxation work; under WPs a
delivered batch holds one source, so the fold never combined across sources.
The absorb-rate controller and "combining is central" are withdrawn.

**Step 6 hypotheses** ([diagnosis](scale-free-diagnosis.md)); "refuted" means
for the measured configurations only:

| Hypothesis | Result | Implication |
|---|---|---|
| H1 bucket resolution on RMAT | Adequate; coarsening helped small runs only | Measure controller overhead at scale |
| H2 hub redundancy | Rejects concentrate at hubs, but hold/fold lost time | Reject counts are not a cost model |
| H3 partition imbalance | No measurable cost to 32 workers; mesh idleness temporal | Reassess active work at scale |
| H4 cadence-limited progress | Large mesh sensitivity; indiscriminate flushing hurts random graphs | Keep gated flushing |

RMAT redundancy may depend on ordering after all: 7.4's idle flush cut
two-node created updates 12.6%. Neither explains the historical RIKEN gap. Pin
the uniform generator's partition jitter in causal A/Bs.

## Algorithm portfolio

| Priority | Kernel | Purpose |
|---|---|---|
| Current | SSSP | Adaptive benefit and distributed competitiveness |
| After Gate A | BFS | Cheapest controller transfer test; compare with direction-optimizing BFS |
| Only for a broader claim | PageRank | Additive residuals, distinct priorities, tolerance convergence |
| Optional | CC | Different progress structure; validate the partition, not the labels |
| Deferred | BC, k-core, triangles | No demonstrated need |

SSSP plus BFS supports a traversal/min-relaxation claim. Shared infrastructure
alone does not show shared adaptive benefit. Use
[GAPBS](https://github.com/sbeamer/gapbs) for validation, not as a kernel quota.

## Evaluation

### Baselines

Use [GAPBS](https://github.com/sbeamer/gapbs) as the *workload and
one-node CPU benchmark*; its suite specifies kernels, graph families, and a
methodology, and its current SSSP is optimized Δ-stepping with bucket fusion.
It cannot serve as a many-node competitor because it is OpenMP shared memory.
Use the RIKEN *implementation* as a distributed competitor on compatible
graphs; using it does not turn this campaign into an official Graph500 score.
Each system gets a documented tuning budget, its own rank/thread layout and
partitioner, the same canonical graph and source, and the same node resources
within a distributed comparison. Preserve the existing matched-input harness.

| Priority | Implementation | Required comparison / reason |
|---|---|---|
| P0, internal | Repaired workshop-era ACIC, 8g ACIC, global-fixed and per-graph tuned-fixed, one-axis ablations | Establish what is new and whether feedback pays rather than just code repair and faster wire format. |
| P0, shared memory | [GAPBS SSSP](https://github.com/sbeamer/gapbs) | Strong one-node reference; report the best one-node time alongside multi-node ACIC time on graphs fitting one node. Tune Δ; record the current bucket-fusion version. |
| P0, distributed | [RIKEN Graph500-SSSP](https://github.com/RIKEN-RCCS/Graph500-SSSP) | Strong distributed Δ-stepping and ACIC's hard scale-free competitor. Tune Δ and process/thread geometry. Mark a graph incomparable when exact integer distances cannot be represented in RIKEN's binary32 path; do not count it as a loss. |
| P0, distributed | [Galois/Gluon SSSP](https://github.com/IntelligentSoftwareSystems/Galois), Async and Sync | Nearest published asynchronous, bulk-communication alternative and a synchronous control. Keep both where the supported adapter validates; compare at the same node counts as RIKEN. |
| P1, shared memory | [Wasp (SC25)](https://research.chalmers.se/publication/549745/file/549745_Fulltext.pdf), artifact DOI [10.5281/zenodo.15872863](https://doi.org/10.5281/zenodo.15872863) | A newer asynchronous priority/work-stealing SSSP CPU comparator that is particularly relevant to roads. Its artifact includes GAPBS and other baselines and many graph conversions. One-node only. |
| P1, shared memory | [GBBS/Julienne](https://github.com/ParAlg/gbbs) or [GraphIt priority SSSP](https://github.com/GraphIt-DSL/graphit/tree/master/graphit_eval/priority_graph_cgo2020_eval) | Add one if the one-node/local-scheduling story is central and the P0 matrix is complete. Do not spend the IPDPS sprint reproducing every one-node algorithm. |
| P2, hardware contingent | [cuGraph SSSP](https://docs.rapids.ai/api/cugraph/stable/api_docs/api/cugraph/cugraph.dask.traversal.sssp.sssp/) (single/multi GPU) and [Gunrock](https://github.com/gunrock/gunrock) | A bounded cross-hardware comparison, with graph fit, precision, device count and setup boundaries stated. The CPU-cluster paper's core evidence is the CPU matrix. |

The [SC22 140-trillion-edge Graph500 SSSP result](https://sc22.supercomputing.org/proceedings/tech_paper/tech_paper_pages/pap112.html)
is an extreme-scale *literature reference*, not a runnable baseline assumed to
exist on the current cluster. Do not compare its GTEPS with an ACIC solve-only
edge rate: graph generator, weights, undirected edge accounting, search-source
protocol, hardware and validation all differ. Its own algorithm also adapts
between sparse and dense communication, so ACIC cannot claim adaptation at
extreme scale as an unoccupied design space. The [November 2025 Graph500 SSSP
table](https://graph500.org/?page_id=238) is a second reference for system
scale; distinguish that ranked submission from the SC22 published experiment.

[Gluon-Async](https://roshandathathri.github.io/publication/2019-pact) already
does asynchronous distributed execution with bulk communication, so the claim
must be about feedback and its measured benefit. Discuss distributed control and
KLA. Stay CPU-focused; GPU and high-diameter advantages are empirical. [Atos is
ICPP22](https://escholarship.org/uc/item/9f17k8gk) ([repo](https://github.com/owensgroup/ATOS));
do not merge its publications into one comparison. Never count an out-of-memory
failure as a slowdown or infer energy from edge counts.

### Inputs

Record a canonical graph identity across systems; a generator name and seed is
not enough. This is the minimum *coverage matrix*, with graph topology and
weight distribution varied separately:

| Priority | Family and examples | Why it matters |
|---|---|---|
| P0 | High-diameter synthetic 2-D meshes at several scales; real [DIMACS road networks](https://www.diag.uniroma1.it/~challenge9/download.shtml), road-ny, road-usa and a held-out road region (e.g. Road-EU) | Main hypothesis. Real roads must use their original positive weights in at least one row; a mesh-only win is insufficient. Include a road weight variant whose maximum distance fits every P0 baseline, without changing topology. |
| P0 | Scale-free RMAT/Kronecker at several scales plus Orkut and a larger social/web graph such as GAP-twitter or GAP-web | The known counter-regime. Retain losses and work-growth analysis as graphs and node counts increase. ACIC's current RMAT is not bit-identical to Graph500's generator. |
| P0 | Uniform random (GAP-urand), at multiple scales | Degree-unskewed control that can expose controller overhead and poor partition assumptions. |
| P1 | A non-grid, sparse high-diameter graph such as a road region, Delaunay triangulation or Kmer-v1r | Test whether the claimed regime is diameter/degree rather than the mesh generator. Wasp's public artifact supplies several of these. |
| P1 | Large real web/social graph (GAP-web/uk-2007/Friendster) and, for a many-node real-data test, [ClueWeb12](https://law.di.unimi.it/webdata/clueweb12/) if its conversion is feasible | Tests the scale-free conclusion beyond tiny Orkut. ClueWeb12 has 978M vertices and 42.6B directed arcs, a useful bridge between one-node GAP graphs and synthetic trillion-edge runs. Report directedness and reachable fraction; do not symmetrize it silently. |
| Stress, separate | Near-zero or highly skewed weights; optional official Graph500-style floats | Tests work explosion and numerical behavior. Do not mix this with the exact-positive-integer GAPBS campaign or call an integer surrogate Graph500-compliant. |

For the main GAPBS-style SSSP matrix, use native positive weights for roads
and reproducible integer weights (including GAPBS's common 1–255 variant) for
graphs without weights. Include at least one second weight regime on the same
topology. Sources are predeclared, several per graph, with reachability and
distance range reported; never select them by speed. Preserve directionality,
duplicates, weights and ID mappings through conversion; time construction,
conversion, solve and validation separately. The current matched-input harness
canonicalizes to undirected min-weight edges, so a directed run needs its own
independently checked adapter and should not be silently pooled with those
results.

For IPDPS, the already prepared mesh24/26, road-usa, rmat25–27, orkut and
uniform25 form the core, with a second real high-diameter graph and at least
one larger web/social graph added only after the P0 validation and ablations.
For SC27, require exact positive-integer weighted SSSP on both undirected and
directed inputs, disconnected components and several held-out sources; document
how loops and parallel edges are handled. Float weights and official Graph500
compliance are separate extensions, each needing its own numerical validation.

**GAPBS versus Graph500:** the [Graph500 specification](https://graph500.org/?page_id=12)
uses an undirected Kronecker graph, 16 edge tuples per vertex, randomized labels,
single-precision weights in [0,1), 64 search roots and a parent-tree validation
contract. The present ACIC/GAPBS campaign uses integer weights, different
RMAT generation and a smaller held-out source set. Report those as GAPBS-style
SSSP measurements; do not claim an official Graph500 run or directly compare
GTEPS to the extreme-scale leaderboard. Graph500's near-zero weights are
algorithmically important: the SC22 study identified them as a cause of deeper
SSSP trees at large scale.

### Methodology rules

1. **Compare:** repaired historical vs current; one global fixed policy from
   development inputs; best fixed per case on separate runs; admission-only,
   delivery-only/local, and joint feedback; targeted ablations including
   negative results.
2. **Freeze controller constants** before held-out graphs, sources, scales and a
   second machine. Report adaptive / best-fixed time (geomean, worst case),
   tuning cost and uncertainty.
3. **State the process geometry and let every system choose its own.** A/Bs do
   not transfer between geometries.
4. **Report the hang rate beside every speedup, and the floor per allocation.**
   A median over survivors scores "stops sometimes" as a win (7.6e read 1.21×
   from a cell that hung twice), and the control-arm floor varies by allocation
   (1.00× to 1.28×).
5. **Randomize arm order**, repeat the baseline as a control, pair
   measurements, use multiple allocations, and never trust a single A/B on
   Anvil. Corroborate diagnostic counters with production counters and bounded
   traces.
6. **Measure edge work, message count and size, memory and exposed controller
   cost** with solve time. Relaxed admission still pays controller bookkeeping;
   a controller-free experiment must preserve progress and termination.

Validation gaps still open: independent cross-system input/result checks and
validation of large runs ([protocol](post-step7-review.md)).

### Scaling and portability

**Two separate questions:** strong scaling asks whether a fixed graph gets
faster as nodes increase; weak scaling asks whether time stays controlled when
graph size grows with nodes. Report both with solve time, edge work, peak memory
and failures; also report load/build time. A spectacular largest run alone is
not evidence of scalability. For very short solves, use larger inputs before
adding nodes. Freeze policy constants before the scale sweep.

| Stage | Suggested graph / allocation | Gate before the next stage |
|---|---|---|
| Validation | rmat26–27 and mesh24–26 on 2/8/16/32 nodes, plus road-usa and a second real graph | Correct and stable on independent sources; comparable P0 baselines; no hidden setup or source-selection effect. |
| Large | RMAT scale 30 and a sparse high-diameter graph on 32/64/128 nodes; add ClueWeb12 if its directed reader and validation are ready | 64-bit counts, no overflows; measured memory headroom and construction cost; meaningful 32→128 strong/weak scaling. |
| Very large | Scale 33 on 128/256/512 nodes, then scale 35 only if a memory model and a smaller pilot support it | All-node run with an independently checked SSSP certificate and at least one distributed baseline on the largest common graph. If only ACIC fits, label a capacity result, not a speedup. |
| Frontier, conditional | 1,024+ nodes or still larger scales only with machine time, memory, and robust validation | Treat as a separate SC27 or later campaign; publish actual graph and machine scale, not an implied Graph500 record. |

At 16 *undirected edge tuples* per vertex, scale 30 has 1.07B vertices and
17.2B tuples, scale 33 has 8.59B and 137B, scale 35 has 34.4B and 550B, and
scale 43 has 8.80T and about 141T (the SC22 order of magnitude). Storing both
directions makes up to twice as many adjacency entries before deduplication.
ACIC's `Edge` uses two 64-bit longs on this platform, so one trillion stored
arcs alone require about **16 TB**, before offsets, distances, work queues,
runtime buffers and construction peaks. Set the scale from a measured per-node
peak and at most ~70% of available aggregate memory, not from edge count alone.
At scale 43, storing both directions of ~141T tuples in ACIC's current `Edge`
would require roughly **4.5 PB for edges alone**; matching the SC22 volume
requires a different memory representation and vastly larger allocation, not
simply extending the 512-node sweep.

**Current code blockers before scale 31+:** `graphlib/gapbs.h` stores .wsg/.sg
destinations as signed 32-bit integers; its 64-bit header counts do not make
that a 64-bit vertex format. More immediately, `WireUpdate` stores the
overflow flag in bit 31 of a 32-bit vertex field and aborts at vertex IDs
≥ 2^31 or tentative distances ≥ 2^32. Scale 31+ therefore requires a wider
wire mode and a distributed 64-bit generator/reader before any solve. GAPBS
and Wasp are one-node baselines and their 32-bit vertex formats cannot cover
the trillion-edge stage. Prove canonical identity and weights across ACIC and
the distributed baseline. Audit all counts, reductions, file offsets,
allocation products and source IDs for overflow; test the boundary just below
and above 2^31 vertices before committing a very large allocation. Remeasure
bytes/update and buffer optima after widening the wire. Audit the starvation gate's
`P × nodes × buffer_size` scaling, per-idle destination scan, hub handler
durations, reduction latency, graph generation/exchange and shared filesystem
traffic. Balanced edge counts are not balanced active work. A second CPU
architecture/interconnect with frozen constants is valuable after the main
scale campaign.

Parked for the first campaign above 16 nodes: rmat20 reads 1.59× and 1.55× for
the `weight` width rule at 16 nodes (floors 1.14×, 1.22×) but regresses at two.
It is the only candidate third entry in `PER_GRAPH_WIDTH_RULE`.

## SC27 schedule after the IPDPS decision

Provisional, from 2026-09-13, assuming early-April submission. The IPDPS
sprint above supersedes this schedule through October 8; its go/no-go gate is
independent of the broader SC27 Gate A.

| Window | Work | Decision |
|---|---|---|
| Sep 13–15, done | 7.5 pilot; 7.6a–e, g, h | Geometry confound; width is a mesh rule; range rejected; deadlock repaired; outcome reporting |
| Sep 15, done | 7.6f2 large-input wave | **Checkpoint 1:** negative on co-design, worth, scaling |
| Sep 15, done | 7.6f1: external re-take, fair layouts, 1–2 nodes | **Checkpoint 2 input:** external systems well ahead; stop condition met, decision pending |
| Sep 15, done | 7.6i: traces of the losing configuration | Per-update pipeline cost, not communication or imbalance: narrowing would have to attack the update rate itself |
| Sep 16, done | 7.6j: per-edge cost | Communication volume was the bound after all; rmat25 within 2.5× of RIKEN per edge, below GAPBS; buffer size must follow graph class; stop decision deferred |
| Sep 16, done | 7.6k controller-chosen buffer size; 7.6l `--enable-shmem`; 7.6m hot spots (two-node A/Bs only) | rmat25 110–116 ns per edge; buffer size follows the graph regime, not the best size within it (orkut) |
| Sep 16, done | 7.6n: re-take 7.6f1 at 1–2 nodes | **Scaling entry condition missed** (RIKEN 2.8×/3.5× ahead on rmat25/orkut); stop decision open |
| Sep 16, done | 7.6o at 2 and 8 nodes, scale-free graphs, with communication shares | RIKEN's lead grows with nodes (rmat25 2.8× → 11.7×); ACIC's work per edge grows 3.8×; step 8 added |
| Sep 17–Oct 11 | Step 8a–8f at 8 nodes (A/Bs only) | Attribution first; stop rule if RIKEN > 5× at 8 nodes after 8b–8d |
| Sep 18, done | 8g: re-take at 2 and 8 nodes, scale-free and high-diameter (16 nodes not entered) | ACIC now faster 2 → 8 nodes on scale-free; RIKEN 1.9–3.7× ahead at 8; ACIC 19–53× ahead of RIKEN, 7–79× of Gluon on high-diameter but flat 2 → 8 |
| by Oct 18 | Gate A decision on 8g | **Gate A:** proceed, narrow (high-diameter only), or stop |
| Oct 19–Nov 15 | Controller extraction and BFS transfer | **Gate B:** transfer, or SSSP-only scope |
| Nov 16–Dec 13 | PageRank only if the claim needs it; else strengthen SSSP/BFS | Freeze algorithm scope |
| Dec–Feb | Scaling, real graphs, baselines, ablations, one portability slice | **Gate C, Feb 15:** evidence sufficient for the chosen claim |
| Now–Mar | Outline now; draft by Feb 22 | **Gate D, Mar 1:** results freeze |
| Early Apr | Abstract, paper, AD at the CFP's dates | Re-anchor when SC27 dates are known |

Record immutable source versions, build/runtime settings, raw results and
figure recipes as work proceeds. Present the repaired 2024 baseline and the new
contribution separately: fixes and refactors are foundation, not evidence of
adaptive control.

## Verification

"Done" means the evidence exists:

- **Integer-kernel refactors:** identical reference results on one and two
  nodes across supported mappings and flags, no unexplained regression. Hash
  equality is a regression check, not independent validation.
- **7.6 / Gate A:** fixed-policy references, held-out adaptive evaluation,
  runtime and work both reported, geometry stated and chosen fairly for every
  system, the checkpoint criteria above answered.
- **Gate B:** each kernel validates independently on at least three graph
  classes; PageRank by tolerance.
- **Campaign:** figures reproducible from raw data; a claim/evidence table names
  limitations and negative results. Where step notes generalize from a small
  matrix, use the qualifications in [post-step7-review.md](post-step7-review.md).
