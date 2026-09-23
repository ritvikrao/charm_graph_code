# Closing the one-node gap

*Started 2026-09-18; updated 2026-09-21 after both two-node batching allocations.
Part of the [IPDPS27 sprint](ipdps27-sprint.md). This revision replaces the
original prospective L1–L3 schedule. Implementation history is in the
[progress log](ipdps27-onenode-progress.md); the completed-job analysis and
raw-data provenance are in [the results report](ipdps27-onenode-results-22222842.md).*

**Current decision:** R0 is complete. R1 nearest priority plus batch 8 produces
large one-node gains and reproducible positive two-node graph-level scaling,
but road work grows 65–72% and one road source does not reliably improve.
The performance-led paper is **not ready**: C6 has not passed, and the new
batched candidate has no eight-node result. Keep batch 8 as the common measured
candidate; R2 is deferred and R3 still requires the author's decision.

**Submission hold, 2026-09-21:** the author requested a plan/status update and
no further jobs. All submitted jobs have completed, Slurm shows no active or
queued jobs, and no new jobs were submitted in this update. Future experiments
below are proposals for a later resumption, not an active submission schedule.

**Resumed, 2026-09-21 evening:** the author lifted the hold and directed work
to continue from "R1 next". The fixed-total-layout attribution experiment,
with its predictions and decision rule, is [recorded before submission](ipdps27-r1-attribution.md).
It runs on Anvil (same EPYC 7763 node shape) with the batch-8 binaries rebuilt
from `48ca9c0`.

**Attribution result, 2026-09-21:** [complete](ipdps27-r1-attribution-results.md).
Road's work growth follows global worker concurrency (W: 1.59–1.84× work when
workers per process go from 7 to 15 at fixed domains and placement). Physical
placement and domain count are within noise. On road, 2 × 8 × 7 beats
2 × 8 × 15 by 11–28%. One-node 16 × 7 beats one-node 8 × 15 by 8–20%, so
scaling denominators must use the best training layout per node count.
Proposed next step: a fixed-layout scaling baseline, then one
concurrency-limiting admission intervention with its prediction recorded;
awaiting the author's choice.

**Layout check, 2026-09-21:** [results](ipdps27-layout-check-results.md).
The Reconverse update to `1233130` regresses road 1.28–1.53x with matching
extra work (mesh 1.03–1.22x); the cause is not the spanning-tree default and
is not yet bisected. On the new runtime, road's best fixed layout (8 x 7 per
node) gets slower from one to four nodes (0.70–0.81x at four), while mesh
scales 1.66–1.86x at four nodes with 16 x 7. Layout conclusions are
high-diameter only and must pass the RMAT regression suite before adoption.

**Cause found, 2026-09-22:** Reconverse's new registered-queue scheduler
(`146ec42`) causes the whole regression. With `+old-scheduler`, road time
and work return to v0916 within control noise. Use `+old-scheduler` for all
performance runs; repeat the 1/2/4-node layout check with it.

| Step | Status | Evidence / next decision |
|---|---|---|
| D0, L1–L3 | Implemented and tested | Earlier candidate improved greatly but failed C6; no automatic L4 |
| R0 | Complete | Redundant work and queue cost quantified at one/eight nodes |
| R1 ordering | Implemented and evaluated | Work falls, but unbatched road regresses at eight nodes |
| R1 batching | Implemented; one/two-node comparisons complete | Batch 8 gives 1.45–1.49x mesh and 1.08–1.15x road speedup from one to two nodes; distributed road work remains unresolved |
| R2 RMAT auto-mode cleanup | Deferred | Secondary to establishing useful high-diameter scaling |
| R3 final acceptance | Not started; author decision required | Batched eight-node performance, held-out cases and full regression gate remain missing |

The [one-node comparison](ipdps27-r1-batch-one-node-results.md) shows batch 8
reducing time 60–61% on mesh and 33–35% on road against frozen R0. The
[unbatched eight-node sanity check](ipdps27-r1-eight-node-check.md) passes
correctness but shows about 8.7x road work growth and no road speedup.
The [completed two-node comparison](ipdps27-r1-batch-two-node-results.md)
validates all 256 performance solves and 96 diagnostic records, plus the
224-solve distributed correctness gate. Mesh scales consistently; road's
aggregate gain is small and one source/denominator comparison is 2.4% slower,
within control variation. This supports the local-cost intervention, not a
claim that large-scale performance has been solved. Defaults and C6 are unchanged.

**Second machine, 2026-09-23:** the campaign now also runs on Frontier
(56 usable cores per node, so Anvil's 16 × 7 has no twin). Inputs regenerate
bit-identically, the mesh C6 result replicates at equal PE count, and road's
bucket-width lever was screened there ahead of Anvil's queued job. Port,
layouts, results and scope are in [ipdps27-frontier.md](ipdps27-frontier.md).
The RMAT regression gate is blocked there: at 8 nodes, whole launches run
fast or about 18% slow at random, in R0 as well, so the frozen/control floor
is a coin flip. The author froze R0 and asked for the cause to be removed
first; nine candidate causes are ruled out
([Frontier §7](ipdps27-frontier.md#7-rmat-launch-bimodality-what-it-is-and-is-not-jobs-55350175535803)).

## 1. Target

The author's requirement is unchanged: a one-node GAPBS or ACIC run must not
beat eight-node ACIC. This is this project's submission gate, not a general
rule that a publishable algorithm must win every benchmark.

**Pass:** for each of `mesh24-z`, `mesh26-z`, `road-usa-z`, and
`road-usa-w4-z`, median paired eight-node ACIC / one-node GAPBS <= 1.0,
no held-out source above 1.2, and median paired eight-node / one-node ACIC
<= 1.0. Four held-out physical sources, three timed repetitions plus warmups,
in **two independent allocations per role**, using the same frozen candidate
binary, flags and per-node layout. Pair each source's median before
aggregating; retain both allocations rather than choosing favorable ones.

The candidate must also pass the frozen/control regression gate on `rmat25`,
`orkut`, `uniform25`, `rmat26`, and `rmat27` at eight nodes in two allocations:
no source slower than the repeated baseline's observed allocation floor
(the rule in `benchmarks/onenode_accept.py`). Correctness and progress gates
are mandatory. GAPBS retains its training-selected thread count, delta and
bucket fusion; ACIC does not get a deliberately weakened reference.

**Stretch:** eight-node ACIC 1.5 times faster than GAPBS and one-node ACIC
within three times GAPBS. Meeting C6 alone does not establish paper novelty,
live-feedback benefit, or a practical advantage over the best feasible system.

## 2. Earlier L1–L3 candidate evidence and its limits

Candidate: `acic_reader_final`, `--process-share auto --reader-tile auto
--slack-control off`. Delta EPYC 7763, eight processes x fifteen workers per
node. All 512 new ACIC solves passed raw-log and independent-digest checks.

| Graph | ACIC 1 node (s) | ACIC 8 nodes (s) | GAPBS 1 node (s), allocations A / B | ACIC 8 / GAPBS, A / B | ACIC 8 / ACIC 1 |
|---|---:|---:|---:|---:|---:|
| mesh24-z | 0.939825 | 0.368624 | 0.097570 / 0.141174 | 3.697 / 2.623 | 0.395 |
| mesh26-z | 3.199109 | 1.003378 | 0.415810 / 0.405541 | 2.481 / 2.477 | 0.308 |
| road-usa-z | 1.257330 | 0.730424 | 0.174827 / 0.199531 | 4.117 / 3.525 | 0.587 |
| road-usa-w4-z | 1.199264 | 0.744314 | 0.200055 / missing | 3.575 / missing | 0.620 |

Seconds are medians of source medians; ratios are source-paired. See the
[results report](ipdps27-onenode-results-22222842.md) for worst sources,
selected GAPBS settings and job IDs. GAPBS B repeats A's settings. A is
22218622 except for weight-scaled road (22222845); B is 22222844.

The candidate cuts time relative to frozen ACIC by roughly 6–11 times at
one node and 3.3–6.9 times at eight nodes, using repetition-paired ratios.
Those improvements retain Charm++. They do not close the remaining
**2.5–4.1 times** median gap to one-node GAPBS. The new eight-node candidate
beats its own one-node execution on all four graphs. Older timings from the
other machine are historical evidence, not interchangeable measurement cells.

The high-diameter candidate has only one allocation at each node count;
the second candidate allocation and full five-graph regression suite remain
missing. Weight-scaled road also needs a second GAPBS allocation. Thus the
observed target cells **fail**, while the complete acceptance protocol is
**incomplete**. Spending on missing cells cannot turn the existing failures
into a pass; defer the full campaign until a revised candidate is promising.

Two RMAT24 allocations show all-auto / all-off ratios of **1.062 and 1.094**
on the same current binary (repetition-paired). All-auto / frozen is 1.087
and 1.086; all-off / frozen is 1.023 and 1.029. Dense graphs resolve these
features off, so investigate mode-dependent overhead. The all-auto arm uses
slack auto, unlike the exact candidate's slack off; isolate that difference
before claiming the candidate itself regresses by the same amount.

## 3. What the diagnosis establishes

The earlier D0 measurements showed that sharing plus tiles plus live slack
reduced inactive participation from 80–88% to 4–17% at eight nodes, and
reduced rounds substantially. That combined variant still made about 33–42
distance changes per vertex; road grew from 10.12 at one node to 42.49 at
eight. The reader pilot helped meshes, but road tiling was 0.4–7.2% slower
than sharing alone and tiling plus slack was 12–14% slower.

These are reasons to investigate work growth and queue cost. They do not
prove that the remaining bottleneck is inherent Charm++ overhead:

- The old post-lever diagnostic has slack **on**, the final candidate has it
  **off**. Measure the exact candidate before attributing its time.
- A PE processing work may perform redundant relaxations or queue/lock work.
  More participation is not necessarily more useful work.
- Successful distance changes are not edge attempts. Count both, and use
  reachable vertices/arcs where appropriate.
- The current work timer includes queue and locking cost. Same-PE/cross-PE
  counts do not identify inter-process or inter-node traffic after stealing.
- Idle participation, round latency and summed work timers overlap. They
  cannot be added into a wall-time partition. Solve time divided by round
  count is not measured reduction latency.

The original budget arithmetic mixed older counts, another layout and
assumed per-change/collective costs. It is retired. The next cost model must
use these same inputs, sources, layouts and executable variants.

## 4. Revised steps and decision gates

**Execution, 2026-09-20:** the author authorized R0–R2 and reserved the R3
decision. [R0 implementation and measurement status](ipdps27-r0-progress.md)
records the completed checks and job history. Stop with a results update after
R0–R2; R3 requires the author's subsequent decision.

All four R0 allocations have completed. The [R1 decision](ipdps27-r1-decision.md)
selects tighter priority across a process's shared queues. Eight-node work
grows 2.58× on mesh and 4.5× on road relative to one node, with broadly similar
measured solver cost per attempt. Preserve tiling's useful parallelism while
testing whether ordering can reduce this rework. The decision records the
counterfactual, disconfirmation criteria and bounded comparison before
implementation; the acceptance gates remain unchanged.
The [R1 implementation and job status](ipdps27-r1-progress.md) records the
opt-in policy and passing repaired verification. Both one-node comparisons
are [complete and valid](ipdps27-r1-one-node-check.md): attempts fall about
80%, but higher queue cost leaves no reproducible road speedup. The two
eight-node jobs are now [complete](ipdps27-r1-eight-node-check.md): mesh
improves, but road becomes substantially slower than frozen R0. The subsequent
[batching follow-up](ipdps27-r1-batch-one-node-results.md) produces a real
one-node timing gain, with a measured work/cost tradeoff. The completed
[two-node comparison](ipdps27-r1-batch-two-node-results.md) reproduces that
benefit but exposes weak road scaling. Preserve batch 8 as the common candidate
and batch 32 as an ablation; avoid another batch-size search. The completed
eight-node jobs use the older nearest-1 implementation. The proposed next
research step is distributed-work attribution below, before secondary RMAT
cleanup or a broad larger-node campaign. Submission remains paused.

Each implementation step gets a separate commit, correctness checks and a
paired comparison. Training sources select parameters; held-out sources
judge a frozen policy. Do not tune a graph-name table against these results.
If these observed held-out sources guide another policy, freeze an additional
unseen source set before evaluating that policy's final claim.

### R0. Completed: quantify redundant work, then cost per operation

Start with the high-diameter gap: measure excess edge attempts, repeated
vertex expansions and stale queue work against GAPBS, and how these grow
from one to eight nodes. Use cost-per-operation measurements to distinguish
redundant work from expensive execution of comparable work. The smaller RMAT
auto-mode penalty is deferred; it is not a prerequisite for this diagnosis
or the intervention it selects.

Run a small matched diagnostic on `mesh26-z` and `road-usa-z` at one and
eight nodes, initially two training sources and two repetitions. Compare
sharing alone with sharing plus reader tiles, keeping slack off. Include an
uninstrumented same-source control to quantify instrumentation perturbation.
Confirm any decision-driving difference in an independent allocation before
acting on it. Use one-node tuned GAPBS on the same CSR and physical sources
as the work-efficiency reference; keep native and scaled road separate.

Collect thread-local counters reduced after timing, and sampled profiles
where available, without adding contended per-edge diagnostic atomics:

| Question | Evidence needed |
|---|---|
| How much work is redundant? | Edge relaxation attempts, successful improvements, vertex expansions, stale queue pops, queue pushes/pops; counts per reachable arc/vertex, with consistent definitions in ACIC and GAPBS |
| What does each attempt cost? | Summed worker CPU time per attempt and samples in edge scanning, atomic updates, queue operations, locks, aggregation/delivery and scheduler code; distinguish busy spinning from useful scanning |
| Does placement trade idle time for work? | Same-binary sharing-only / sharing-plus-tiles, work counts, participation and actual intra-process, inter-process and inter-node delivery counts/bytes |
| Are rounds on the critical path? | Readiness-to-reduction and reduction-to-pickup timestamps, admitted-work availability, and an overlap-aware critical-path trace or bounded counterfactual |
| Is the cost algorithmic or runtime-specific? | Compare work count and CPU cost separately with GAPBS; queue/priority and message policy differences must remain explicit |

Do not infer that avoiding Charm++ would solve the gap merely from time in
message handlers: those handlers also execute algorithmic work. RIKEN's
scale-free loss is a separate claim and needs matched work counts before a
causal explanation; it is not a reason to broaden this sprint's optimization
scope. A simple runtime microbenchmark alone cannot establish an end-to-end
SSSP limit either.

**Exit:** a table and profile explain the dominant remaining costs of the
exact candidate and support a testable counterfactual with enough potential
to address the measured gap. Report uncertainty and instrumentation overhead.
If the evidence cannot identify such a path, stop the performance sprint.

### R1. Implemented: priority and the authorized batching follow-up

| If R0 isolates… | Candidate intervention | Required causal check |
|---|---|---|
| Excess relaxations/expansions | Tighter priority discipline or admission in shared work queues | Work falls enough to offset lost participation; compare against a strong fixed policy |
| Queue/lock/delivery cost per attempt | Coarser batches or cheaper shared-queue access | Cost per attempt falls with comparable work counts; distinguish this from ordering effects |
| Concentration caused by placement | A bounded tile-granularity rule selected from training/read-time metadata | Gain survives independent sources and allocations on roads as well as meshes; account for increased cut and rework |
| Exposed controller latency after the above accounting | L4, a bounded round/pickup experiment | Critical-path cost and time-to-solution fall while work and progress remain controlled |

Choose one row, not all four. State the predicted gain, measured limiting
cost and disconfirming result before implementation. A live controller must
beat the same mechanism at a training-selected constant; comparing it only
with a weaker old baseline does not establish C3c. If its plausible benefit
cannot close the remaining gap, it does not trigger another C6 campaign.

### R1 next. Attribute distributed road work before expanding scale

**Proposed only; submissions are paused.** The two-node experiment meets the
limited aggregate direction of its training hypothesis in both allocations.
That does not establish uniformly useful road scaling: doubling workers adds
65–72% work, and one source has no reliable gain. Batched eight-node behavior
cannot be inferred from the older unbatched run or by multiplying observed
one-node batching gains into old scaling curves.

1. Preserve the current production/diagnostic binaries, batch 8, sharing auto,
   reader tiles auto, slack off, and all completed results. Keep the original
   8 processes x 15 workers per node as the main scaling baseline. No new
   batch-size tuning, graph-name policy or default change.
2. Review existing source-level work, controller and queue diagnostics. If
   experiments resume, first separate increasing the number of process-local
   priority domains from spreading the same domains across physical nodes.
   A concrete diagnostic pair is **16 total processes x 7 workers** on both
   layouts: 16 processes on one node versus 8 per node on two nodes. This
   holds total workers, priority domains and logical partitioning fixed;
   verify the resolved reader layout and use the same graph/source/flags.
   It is an attribution experiment, not a replacement tuned performance
   baseline. Physical placement also changes memory locality, so the pair
   alone cannot prove a pure network-latency explanation.
3. Measure edge attempts, repeated expansions and distance-order/arrival
   diagnostics alongside delivery/controller behavior. If work grows when
   only physical placement changes, investigate remote delivery and admission
   timing. If it does not, investigate the extra priority domains and changed
   partitions exposed by adding workers. These are hypotheses to distinguish,
   not established causes and not evidence of an inherent Bellman–Ford limit.
4. Select at most one targeted distributed-order intervention only after that
   attribution, with a recorded prediction and a disconfirming test. Compare
   against the unchanged batch-8 candidate at the same worker/node counts.
   Favor a reduction in work that produces a repeatable time gain on both
   training sources; do not exchange a large work increase for a noisy small
   timing win. No new intervention is selected or implemented in this update.
5. A later four/eight-node batching pilot should follow evidence of a credible
   scaling path, with the same fixed candidate at one node as its denominator,
   independent allocations, and all per-source regressions retained. A limited
   scaling pilot is distinct from the full R3 acceptance campaign. Revisit
   R2 only once that evidence justifies preparing a final candidate.

### R2. Address dense-mode cost after a promising intervention

Run this step only after R1 produces a candidate worth taking to final
acceptance. The 6–9% all-auto RMAT penalty is secondary to the 2.5–4.1 times
high-diameter gap. It must not delay R0 or R1; if the high-diameter route
stops, do not pursue this repair as part of the performance sprint.

Use RMAT24 to compare explicit off, the exact candidate flags, each auto
flag separately, and all-auto, within the same binary/allocation. Keep a
repeated frozen control and rotate arm order. Resolve which flag matters
before assigning the slowdown to sharing, slack, or layout.

A concrete first hypothesis is repeated density/mode evaluation:
`process_share_active()` and `slack_control_active()` are called in solve
paths even when dense-graph auto selects off. Inspect/profile those paths;
if implicated, resolve the decisions once after graph metadata is available
and distribute consistent immutable decisions to every process/PE. Preserve
explicit-on behavior, graph-reader ordering and multi-source resets.

**Exit:** equivalent effective settings and correct digests, plus the
mode-dependent penalty removed within repeated-control noise in two
allocations. Include before/after, exact-candidate and explicit-off arms;
do not call a 2–3% current-off difference harmless without measuring its
floor. This is an engineering repair, not evidence that live adaptation
improves SSSP, and RMAT24 is not the full regression gate.

### R3. Freeze and accept after R1 and the R2 regression check

Run the full protocol in §1, with both node counts, independent allocations,
frozen tuned GAPBS settings (including a second weight-scaled road run), and
the five-graph regression suite. Retake only the causal ablations and external
cells needed for the final claims. Use the existing multi-source harness when
compatible; retain identical timing boundaries and check source independence.

The acceptance tool currently expects one GAPBS job ID per allocation for
all target graphs. The present road-w4 selection is in a separate job; future
acceptance jobs should collect all four frozen selections together (or extend
the tool to support explicit per-graph IDs with provenance checks). Do not
fabricate a combined job or count one candidate allocation twice.

## 5. Disposition of the original levers

### L1. Tiled vertex placement (over-decomposition by tiles)

Offline relabeling and reader tiling are implemented and verified. Reader
mapping preserves original source/digest IDs and does not change the runtime
destination-table lookup. Mesh pilots benefit; road does not yet show a
consistent benefit. The current 64-tiles-per-owner auto policy is a measured
candidate, not an adopted universal sparse-graph rule. R0 decides whether to
retain it on the basis of placement, work and cost together.

### L2. Shared state within a process

Implemented: process-local atomic distances, bucketed work sharing/stealing,
and intra-process relaxation bypassing htram. Cross-process delivery remains
aggregated. Large gains over the frozen build are established in the campaign,
but the final candidate still lacks its full adoption gate. Batched sources
with CSR retention and per-solve state reset are implemented and verified.

### L3. Live control of how far ahead a PE may run

Implemented and correctness-checked. Existing pilots do not establish a
reliable gain over strong fixed settings; road can regress. Keep slack off
in the measured candidate. Live feedback remains an unproven contribution,
not a promised result of having implemented L3.

### L4. Cheaper rounds (only if D0 says rounds still bind after L1–L3)

Not triggered. Existing D0 counters do not isolate exposed round cost after
sharing/tiling. R0 must establish it before R1 selects L4. A process-level
controller still has to preserve the conservation ledger, progress guarantees
and pickup semantics; a stale contribution from one ready PE is not a valid
substitute for unfinished peers.

### Not pursued in this sprint

No automatic L4 implementation, MPI rewrite, additional kernel, larger graph
or >8-node campaign as a substitute for passing the existing cases. No weaker
GAPBS configuration. No claim of inherent runtime unsuitability, universal
robustness, or easier programming from performance ranks alone.

## 6. Schedule and stop rule

**All submissions are paused by the author's latest instruction.** The dates
below are research decision targets, not authorization to run jobs or resume
work automatically.

| When / state | Deliverable / decision |
|---|---|
| Complete | R0 work/cost attribution; R1 priority and one/two-node batching comparisons; unbatched eight-node audit |
| Current hold | Preserve the measured batch-8 candidate and raw evidence; no further submissions |
| If the author resumes experiments | Focused distributed-work attribution on road, then one justified intervention or bounded scaling pilot |
| Only after a credible scaling candidate | R2 isolate/address RMAT auto-mode cost; retain the full regression gate |
| 09-25 target, conditional on evidence and resumption | Freeze a promising candidate and policy, or stop the performance sprint |
| **09-26 decision target** | C6 go/no-go; current missing batched eight-node/held-out evidence does not support a submission claim |
| 09-27–30 target, only after a promising first pass and the author's R3 decision | Full acceptance, regression/claim checks and final paper decision |

Cap **R0–R2 combined at 2,000 additional SU**, not an automatic expenditure.
Prioritize the R0 diagnosis and R1 intervention; spend on R2 only if that
route produces a promising candidate.
Estimate each job from allocated cores and its wall-time limit before
submission; use small training panels and stop ineffective arms early. If the
planned diagnostic cannot fit, narrow it rather than silently extending the
budget. The former unconditional ~5,000-SU E1/E3 retake is deferred until R3
has a justified candidate; do not spend it repeating known failing cells.

If no single diagnosed intervention plausibly closes the gap, or C6 still
fails on September 26, **the current performance-led IPDPS submission is
no-go**. Carry the evidence into the SC27 research decision, not an automatic
larger optimization campaign. A new paper direction needs its own explicit
hypothesis and evidence gate: for example, a mechanism that predicts the
priority/locality/redundant-work tradeoff on unseen inputs, with novelty beyond
the workshop paper and existing asynchronous priority/work-sharing work.
A negative characterization needs a generalizable result; being consistently
second or mapping mechanisms to Charm++ APIs is insufficient by itself.
