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
