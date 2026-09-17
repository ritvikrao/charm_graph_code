# Review after step 7

*2026-09-13. Planning review only: no application, benchmark, build, test, or job
was run, and no code was changed. This review uses the committed design notes,
selected saved outputs, and read-only inspection of the solver and harness.
The 2024 paper's referenced `~/Downloads/acic_2024paper.pdf` is absent on Delta;
historical paper results below are those reported in the existing plan.*

**Follow-up:** the subsequent [step 7.5 comparison report](step75-comparisons.md)
records the executed pilot, baseline setup, measurements, and failures. The
text below preserves the pre-experiment review; use the updated
[working plan](sc27-plan.md) for the next actions.

## Recommendation

**Insert a short SSSP comparison and adaptivity study before steps 9–13 (numbered 8–12 when written).** Start
external comparisons now, use them to choose at most one or two further
optimizations, then generalize the mechanism that survives. A large framework
refactor does not resolve the current research uncertainty.

The promising contribution is **feedback that jointly controls work admission
and delivery latency**. The immediate question is whether this feedback finds
good operating points across workloads and scales without per-input tuning.
Combining, type erasure, and a long algorithm list are not prerequisites for
answering it.

## What the evidence supports

| Finding | Evidence | Consequence |
|---|---|---|
| Adaptive round-boundary flushing is the largest step 7 gain | [7.1](step7-flush-cadence.md): 3.6×/3.8× mesh speedup on one/two nodes against the previous fixed policy | Keep it, but compare against a tuned fixed policy; flushing every round already reaches this mesh performance |
| Combining is currently a negative result | [7.2](step7-combining.md): hold 1.17–1.39× slower on RMAT; fold 1.09–1.14× slower | Keep both off; drop the proposed absorb-rate controller and the claim that combining is the central mechanism |
| Bucket adaptation has limited demonstrated reach | [7.3](step7-bucketing.md): confirmed 1.14× mesh and 1.11× RMAT gains at 2^20 on one node, unresolved at 2^22 on two | Treat this as a candidate policy, not evidence of general adaptive optimality |
| Delivery timing can change useful work | [7.4](step7-idle-flush.md): two-node RMAT created updates fall 25.05M → 21.90M, with 1.12× speedup | Reopen the ordering/latency interaction; step 6 did not establish that RMAT work is intrinsically ordering-independent |
| The same intervention can increase speculation | 7.4: two-node mesh rounds fall 15%, but created updates rise 17%; runtime is nearly flat | This tradeoff motivates joint control; flushing more often is not universally better |
| Experimental coverage remains narrow | Step 7 uses three synthetic classes, one/two nodes, mostly 2^20, 16 workers/node, seed 1, one deterministically selected source per graph | Real graphs, source diversity, larger problems, and node occupancy now have higher value than another unconditional optimization |

The old **2.8–3.3× RMAT deficit is historical**, not a measurement of the current
code against RIKEN. Step 3's reported 2.5–4.1× defect-fix gain was on a laptop,
on a different workload. Neither that gain nor the step 7 ratios can be used to
calculate today's external gap. Likewise, do not multiply sequential A/B gains
from different jobs into a cumulative headline speedup. Measure the final
configuration directly against a pinned, repaired pre-step-7 baseline.

Two interpretations in the earlier notes need qualification. High reject counts
do not establish that rejects dominate runtime, as combining demonstrated.
Also, instrumentation can change the schedule of an asynchronous algorithm and
therefore its work counts. Keep production timings and detailed diagnosis runs
separate, but check that lightweight production counters corroborate the
diagnostic mechanism. A matching final digest does not establish an unchanged
execution schedule.

## Step 7.5: establish the current competitive position

Budget roughly **two working weeks**, adjusting for allocation access. The
deliverable is a small decision report, not the final paper campaign.

### First close the validity gaps

- **Termination:** `Main::reduce_histogram` still requires
  `updates_created > 1000`. The [verification note](verify-harness.md) documents
  that an isolated source does not terminate. This also excludes runs with at
  most 1000 created updates. Plan a liveness/correctness fix and small-component,
  isolated-source, disconnected-graph, and delayed-delivery checks before using
  arbitrary sources. Do not just remove the guard without establishing that
  the asynchronous accounting safely detects termination and drains held work.
  A timeout must remain a failure, never a converged timing sample.
- **Input identity:** use the same edges, weights, directionality, duplicate
  policy, source IDs, and numeric range in every implementation. A common RMAT
  scale and seed do not guarantee the same graph. The local graphlib generates
  directed edges and preserves duplicates; other generators may symmetrize or
  deduplicate. Verify conversions independently, preserve vertex-ID mappings,
  and compare full small-graph distance vectors to an independent solver.
- **Scale validation:** the serial verification gate does not validate large
  runs. Plan an independent reference comparison where feasible and a scalable
  shortest-path certificate for larger inputs: source distance, unreachable
  handling, edge inequalities, and source-rooted witness paths for finite
  labels. Edge inequalities alone are insufficient. Validate each measured
  result outside the timed solve, including every adaptive variant.
- **Measurement:** randomize or balance variant order within each allocation
  block and retain the repeated-baseline control. The current harness repeats
  the same variant order; interleaving alone does not remove its measured ~5%
  position effect. Use consistent warmups, paired ratios with uncertainty, and
  independent allocations. More repetitions of the same ordering and source
  do not establish generality. Failure to resolve a difference is not an
  equivalence or non-regression result.

### A small, relevant baseline set

| Priority | Baseline | Question answered |
|---|---|---|
| Immediate | Repaired pre-step-7 ACIC, current defaults, fixed-policy ACIC, and admission-relaxed ACIC | What do the new mechanisms add within the same implementation? |
| Immediate | [RIKEN Graph500-SSSP](https://github.com/RIKEN-RCCS/Graph500-SSSP), present at `~/Graph500-SSSP` | Has the historical distributed Δ-stepping gap survived the repairs and policies? |
| Immediate | [GAPBS SSSP](https://github.com/sbeamer/gapbs) on one node | Is the local execution efficient against an optimized shared-memory implementation? |
| Next | [Gluon-Async and Gluon-Sync distributed SSSP](https://iss.oden.utexas.edu/?p=projects/galois/analytics/dist-sssp) | Does the method improve on an existing distributed asynchronous system? |
| Next, one node | [Wasp, SC25](https://research.chalmers.se/en/publication/549745) | Is there a local priority-scheduling advantage or deficit hidden inside the distributed result? |

These are complementary baselines, not a claim that any one is universally the
fastest implementation. Wasp is multicore work on priority-aware work stealing;
it is not a distributed scaling baseline. Gluon-Async explicitly combines
continuous computation with bulk communication, so it is a closer comparator
than Gluon-Sync alone. Its authors report large-diameter experiments; road
graphs are contested territory. See the
[Gluon-Async paper](https://roshandathathri.github.io/publication/2019-pact).

Audit the RIKEN input path early. Its public interface documents a generated
Graph500 problem, a tunable delta, and a preference for a square process count;
it does not establish that the local copy can consume the same weighted real
graphs as ACIC. Budget minimal input adaptation if needed, without changing the
baseline algorithm. Start GAPBS and internal comparisons while that is resolved.
Never silently compare different generated graphs. Respect each baseline's
supported process layouts and tune delta in the actual weight units.

### Pilot matrix and fairness

Begin with mesh, RMAT, and uniform controls, **one genuinely weighted road
graph**, and **one social/web graph** with explicitly assigned weights. Use
2^20 only as the link to previous experiments; include 2^22 and a larger size
chosen to keep solves measurable and within memory. Start with 1/2 nodes,
then 4/8/16 as allocations permit; expand the most informative cells rather
than running the full Cartesian product at once. Include a weak-scaling slice
once the first strong-scaling bottleneck is understood.

Retain 16 workers/node for continuity, then test at least one higher occupancy
and an appropriate full-node layout. Compare equal allocated nodes and record
active compute/communication cores, ranks, threads, NUMA binding, and memory.
Allow each system a sensible layout within the same resource budget; forcing
identical rank counts can handicap a baseline. Pin and record compiler,
Charm++/Reconverse, htram, MPI, transport settings, and both repository commits.

Start with several predeclared graph seeds and about eight distinct sources per
input where possible, reporting reachable vertices and reachable outgoing edges.
Choose performance sources by a common stated eligibility rule, not by observed
speed; include isolated/small-component sources in correctness coverage. Add
sources and allocations if uncertainty remains material. This is a pilot;
use the official source-selection and repetition rules if later claiming GAP
or Graph500 benchmark compliance.

Report solve time including control and termination, graph construction/loading
separately, preprocessing/reordering, peak memory, messages and message sizes,
and actual edge examinations/created updates. Define every work counter before
comparing implementations; receiver rejects, successful distance changes, heap
pops, and outgoing edge scans are different operations. Distinguish total
runtime-message payload bytes from off-node network bytes; the existing htram
`bytes sent` counter is not a hardware wire-traffic measurement. Include
controller time/traffic and report a work/time tradeoff rather than equating
fewer edge scans with lower total cost or energy.

## Step 7.6: test adaptivity, then optimize the measured bottleneck

Budget **two to three working weeks**, with one causal study and at most one or
two implementation candidates. First run a compact sweep of existing controls
on the current stack: heap admission, tram admission, buffer size, and flush
cadence. The old `p_tram` repair sweep and buffer-size performance measurements
are local-machine evidence; two-node envelope correctness checks do not replace
a multi-node performance sweep.

### Separate a good adaptive method from a good default

Compare all of the following with the same finite tuning budget:

1. The repaired legacy settings and current defaults.
2. **One global fixed configuration**, chosen using only a development set.
3. **Best measured fixed configuration per workload**, with selection runs
   separate from confirmation runs. This is an empirical reference within the
   searched configurations, not a proven optimum.
4. Local-only buffering feedback and admission-only feedback.
5. The shared-feedback policy controlling admission and communication together.

Hold out graph instances/classes, sources, scales, and later one machine. Freeze
controller constants before evaluating them. Report
`T_adaptive / T_best_measured_fixed` per case, geometric-mean overhead, worst
regression, tuning cost, and the fraction of cases outside a predeclared band.
A useful **provisional engineering target** is within 10% geometrically and
within 20% on every held-out case, subject to measurement resolution. These
numbers are decision thresholds, not existing results or publication rules.

Show how actions change over a run and whether they follow changing conditions.
The current bucket policy only coarsens; its initial width is still selected
using graph size/mode. Test sensitivity to that initialization and to phase
changes before calling it a general controller. A near-open percentile setting
still pays histogram/reduction costs and is not a true controller-free baseline.
Distinguish relaxed admission from eliminating control overhead while retaining
safe progress and termination.

The decisive experiment is a small **admission × communication-policy** matrix,
with fixed and adaptive choices on each axis. Also compare independent local
control with the shared global signal. If joint feedback adds nothing beyond
two independent policies or one fixed setting, narrow the co-design claim.

### Highest-priority mechanism: buffer residence time versus speculative work

Use existing buffer/flush settings to perturb delivery time, then measure buffer
residence age (sampled where instrumentation is needed), queue wait, useful
edge work, messages, and round latency on RMAT and the mesh/road class. Repeat
the 7.4 effect at larger sizes and 4–16 nodes. Changing flush policy changes both
ordering and traffic; the saved counters support a hypothesis about latency,
not yet a direct measurement of where the latency lies.

If that relation survives, try a **bounded-age flush policy** coupled to runnable
work/admission, using occupancy or recent fill rate to avoid repeatedly sending
tiny messages. Compare it with fixed small buffers and a fixed time-based flush.
Include the cost of clocks, sampling, policy decisions, and control traffic.
Use bounded updates, hysteresis, a minimum dwell period, and a progress fallback
if a policy is introduced; do not tune several coupled controllers at once.

### Scale risks to measure before the framework refactor

- **Starvation threshold:** the current gate compares the histogram window with
  `P × D × B`, where `P` is total workers, `D` destination nodes, and `B` buffer
  capacity. At fixed workers/node `q`, this grows as `q × D² × B`. Actual active
  streams need not grow that way. This creates a plausible scale-dependent
  tendency to flush, not a demonstrated failure. Measure active streams, buffer
  occupancy/age, fraction of time gated on, and messages per useful update;
  consider an active-stream estimate only if the current signal misclassifies.
- **Idle callback cost:** under WPs, `flushIdle()` scans destination nodes on
  every idle scheduler pass. The O(D) scan may matter at scale. Trace its exposed
  cost before replacing it with an active-destination list or rate limit.
- **Hub fan-out and progress:** a high-degree vertex may monopolize an entry
  method even when total edges are well balanced. Measure handler durations,
  scheduler/communication delay, and edge work per worker. If this dominates,
  bounded edge chunks or local scheduling changes are a better first experiment
  than migrating whole chares. Hashing vertex owners does not split one hub's
  adjacency list.
- **Controller cost:** measure reduction/broadcast latency and computation that
  continues between decisions. Global nonblocking collectives still cost time;
  distinguish barrier-free work execution from the absence of global coordination.
  Try reduced control frequency or hierarchy only if exposed control cost grows.

Do not resume combining based on absorb rate alone. Revisit it only if larger
graphs/nodes establish a communication bottleneck and avoided traffic has a
plausible benefit exceeding the measured lookup/holding cost. The negative
one/two-node result is strong evidence for the default, not a theorem about all
aggregation designs and networks.

## Decision gate and generalization

**Gate A, after the pilot and one bounded optimization cycle:**

- If shared feedback beats the best global fixed choice across held-out regimes,
  stays close to per-case tuned choices, and has credible external runtime
  results, proceed to a second kernel and the scaling campaign.
- If real road/mesh inputs show a substantial benefit but RMAT remains slower,
  consider a narrower paper about latency-sensitive distributed traversal.
  Report the RMAT limit. An arbitrary requirement to be within 1.5× of RIKEN
  on RMAT should not alone decide the value of that contribution.
- If a single fixed configuration matches the adaptive system, or improvements
  vanish at representative scales, stop adding generic infrastructure. Diagnose
  the failed adaptivity hypothesis or frame the result more narrowly.

The research needs both an advantage from adaptation and a credible external
comparison. Internal speedups alone establish neither. Conversely, outperforming
RIKEN would not by itself show why adaptive control is necessary.

For the broad distributed fine-grained graph claim, keep a second kernel as a
gate, not an optional appendix. **BFS is the cheapest transfer test**, although
SSSP plus BFS supports only a traversal/min-relaxation claim. Compare BFS with
direction-optimizing implementations, not just naive push BFS. If the desired
claim includes residual-based algorithms, prioritize **PageRank** as the next
conceptually different test after BFS. CC is an optional contrast with different
priorities, not a guaranteed combining success.

Extract the controller interface around that second use. Do type erasure only
when different payloads make it necessary. Separate correcting PE/chare identity
assumptions from adding overdecomposition or migration; require correctness
across every supported mapping, not just K=1. Require a measured benefit before
carrying K=8/hash placement into the paper.

PageRank needs its own residual/termination contract, damping, dangling-node
handling, and tolerance-based validation. Floating-point sum is not exact or
order-independent; an additive delta must be accounted for once, including
while buffered. Do not apply SSSP's byte-identical digest gate to it. Defer BC,
k-core, and triangle counting; k-core is not generically a min-label update and
would need a separate algorithmic contract.

## Positioning and schedule corrections

Keep CPU implementation as the core. Plan a bounded GPU comparison after the
CPU pilot if the final claim warrants it; do not make three GPU stacks, four
machines, or 512 nodes prerequisites for learning whether the method works.
Memory-capacity experiments should separately report in-memory and supported
out-of-core execution. An out-of-memory failure is not a timed slowdown.

Remove the blanket claims that GPUs only win through extra work, that road
graphs necessarily favor CPUs, and that scalable asynchronous GPU processing
is unoccupied. Gluon-Async is prior work in that space. The Atos citation also
needs separation: the [task-parallel scheduler paper is ICPP22](https://escholarship.org/uc/item/9f17k8gk),
while its [repository](https://github.com/owensgroup/ATOS) includes multi-GPU
BFS/PageRank and communication aggregation; the broader multi-GPU treatment
appears in the author's [2024 dissertation](https://escholarship.org/uc/item/74w2x0s9).
These are related work to distinguish precisely, not evidence for a universal
CPU advantage.

The old deadline assumption is wrong: the official
[SC26 dates](https://sc26.supercomputing.org/all-dates-deadlines/) list **April 1
abstracts, April 8 papers, and April 28 the mandatory AD appendix**. April 27
was a tutorial/panel deadline. This review could not confirm an SC27 paper CFP.
Use **early April 2027 as a provisional planning boundary**, plan for abstract
submission separately, and update all dates once the official CFP is available.
Reserve results freeze around early March rather than April 2.

Begin the paper outline and a claim/evidence table now. Preserve the repaired
2024 baseline and describe the new contribution beyond the workshop paper;
bug fixes and mechanical refactors should not be counted as adaptive-method
gains. Resolve the scope by evidence in autumn rather than waiting until the
old December/February gates.
