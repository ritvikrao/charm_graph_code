# ACIC forward plan

*Decision plan from 2026-09-23. Earlier schedules remain in Git history. Read
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

Build the paper around four figures/tables:

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
- No title or claim says live adaptation caused the mesh result.

A practical ten-page budget is 1 page introduction, 1 background/predecessor,
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
