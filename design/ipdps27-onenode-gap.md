# Closing the one-node gap

*Started 2026-09-18; revised 2026-09-19 after jobs 22222842–22222847.
Part of the [IPDPS27 sprint](ipdps27-sprint.md). This revision replaces the
original prospective L1–L3 schedule. Implementation history is in the
[progress log](ipdps27-onenode-progress.md); the completed-job analysis and
raw-data provenance are in [the results report](ipdps27-onenode-results-22222842.md).*

**Decision:** D0 and L1–L3 are implemented; the measured candidate still fails
C6 on every high-diameter graph. The performance-led IPDPS paper is **no-go
as-is**. Continue only with a bounded attribution pass and, if justified,
one targeted intervention. Do not start L4 merely because C6 failed.
All three new feature defaults remain off.

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

## 2. Completed evidence and its limits

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
records completed checks and pending jobs. Stop with a results update after
R0–R2; R3 requires the author's subsequent decision.

Each implementation step gets a separate commit, correctness checks and a
paired comparison. Training sources select parameters; held-out sources
judge a frozen policy. Do not tune a graph-name table against these results.
If these observed held-out sources guide another policy, freeze an additional
unseen source set before evaluating that policy's final claim.

### R0. Quantify redundant work, then cost per operation

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

### R1. At most one intervention selected by R0

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

Replace the old open-ended L1–L3 implementation schedule with these gates:

| When | Deliverable / decision |
|---|---|
| Completed review, 09-19 | Archive validated results; record C6 failure and incomplete acceptance; revise paper claims |
| Next, before further performance changes | R0 redundant-work analysis and matched cost attribution on the high-diameter graphs |
| By 09-24, only if justified | One R1 intervention, correctness gate and independent paired A/B |
| Only after a promising R1, before final freeze | R2 isolate/address RMAT auto-mode cost; retain the full regression gate |
| 09-25 | Freeze a promising candidate and its policy, or stop the sprint |
| **09-26** | Initial C6 go/no-go; an incomplete or failed target does not authorize a submission claim |
| 09-27–30, only after a promising first pass | R3 second allocations, full regression/claim checks, final paper decision |

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
