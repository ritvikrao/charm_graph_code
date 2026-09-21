# R1 batching: first two-node allocation

2026-09-21. **Job 22284705 completed and passes the full audit.** Batch 8
improves time over optimized one-node execution on both training graphs in
this allocation. Mesh gains are substantial; road gains are modest because
edge work grows about 65%. The second allocation, **22284706**, remains pending.
This is a preliminary result, not a completed reproducibility gate.

## Validation and accounting

Job 22284705 completed with exit 0 in **345 seconds** on **cn016 and cn088**,
using 256 allocated cores and 240 worker threads. Allocated CPU time was
**24.533 hours**. All **128 solves**, **48 diagnostic records** and **64
launches** pass a fresh raw-log audit, which agrees exactly with the audit
produced inside the job. Digests, source ordering, binary hashes, effective
flags, worker layout, complete matrices and queue/edge/topology accounting
all pass. No new runtime errors, stalls or rescues were found; the existing
UCX registration-cache warnings remain.

The distributed correctness gate **22284699** also received a full raw-log
recheck: **224 serial/parallel digest matches**, **112 diagnostic accounting
checks**, and **90 connected diagnostic solves with positive inter-node work**.
It completed with exit 0 in 120 seconds on cn064/cn066, consuming 1.067 allocated
CPU-hours. Both binary hashes match the completed one-node experiment.

Two-node A shares cn016 with one-node A, but ran in a separate allocation.
All scaling comparisons retain both one-node allocations independently; they
do not select a favorable machine or denominator. Warmups are validated and
excluded from medians. Each source has three timed repetitions, and each graph
has two training sources. Source medians are paired before aggregating ratios.
Speedup below is the median of paired one-node/two-node time ratios.

## Scaling from the optimized baseline

| Graph | One-node batch 8 time, A / B | Two-node batch 8 | Speedup vs A / B | Two/one edge-work growth, A / B |
|---|---:|---:|---:|---:|
| mesh26-z | 1.116 / 1.150 s | 0.774 s | 1.448 / 1.488x | 1.230 / 1.228x |
| road-usa-z | 0.748 / 0.729 s | 0.651 s | 1.151 / 1.123x | 1.648 / 1.650x |

This corresponds to about **72–74% parallel efficiency on mesh** and
**56–58% on road** when doubling nodes. Batch 8 attempts/edge rise from
1.533–1.536 to **1.885** on mesh and from 2.929–2.932 to **4.845** on road.
Every source median improves against both one-node allocations. The weakest
case is road source 1294456 against one-node B: **0.715004 to 0.679384 s**,
only a 5.0% time reduction. That source needs confirmation in the second
allocation; the aggregate road improvement is not a uniform large win.

The repeated batch-8 control takes **0.745 s on mesh** and **0.650 s on road**.
Its paired control/candidate ratio is **0.965 / 1.001** respectively, with
source-level differences of -5.2% to -1.8% on mesh and -2.8% to +3.0% on road.
The corresponding speedups over the two one-node baselines are 1.501–1.543x
on mesh and 1.121–1.150x on road. Keep the primary arm and repeated control
separate; do not replace the primary time with the faster copy.

## Batching and ordering at two nodes

| Graph | Frozen R0 | Nearest / 1 | Nearest / 8 | Nearest / 32 |
|---|---:|---:|---:|---:|
| mesh26-z | 1.759 s | 1.752 s | 0.774 s | 0.708 s |
| road-usa-z | 0.923 s | 1.117 s | 0.651 s | 0.700 s |

Batch 8 / nearest-1 paired time ratios are **0.440 on mesh** and **0.584 on
road**. Against frozen R0 they are **0.440 and 0.705**. Batching's local timing
benefit therefore survives crossing a node boundary in this allocation.
Unbatched nearest remains 21% slower than frozen R0 on road at two nodes.

Batch 32 is **8.6% faster on mesh** and **7.5% slower on road** than the primary
batch-8 arm, while performing **35% / 69% more work**. Its attempts/edge are
**2.551 / 8.189**. Against its own one-node baseline it achieves **1.36–1.37x
speedup on mesh** and **1.11x on road**, with work growth **1.36–1.37x / 1.79–1.81x**.
This does not justify replacing the common batch-8 candidate with graph-specific
settings. The mesh timing advantage versus the repeated batch-8 control is
smaller, about 5.2%; both controls remain visible.

The new experiment includes nearest-1 diagnostics in the same allocation and
binary as both batching arms:

| Diagnostic metric | Mesh nearest/1 | Mesh batch 8 | Road nearest/1 | Road batch 8 |
|---|---:|---:|---:|---:|
| Solver work ns / attempt | 774 | 318 | 1,024 | 457 |
| Estimated queue ns / attempt | 551 | 183 | 765 | 294 |
| Queue share of measured solver work | 71.2% | 57.4% | 74.6% | 64.6% |
| Consumed entries per removal call | 0.89 | 5.35 | 0.90 | 5.39 |
| Failed try-lock probes | 23.2% | 8.7% | 23.6% | 10.5% |

This directly supports the coordination-cost mechanism. Compared with the
same batch-8 diagnostics on one node, solver cost per attempt is broadly
similar: **304–305 to 318 ns on mesh**, **464–474 to 457 ns on road**. Road
queue cost per attempt also stays near 294–297 ns. Growing operation count,
not a large rise in per-attempt cost, is the main measured obstacle to road
scaling. At unchanged cost and perfect balance, 1.65x work on twice the
workers would yield only about **2/1.65 = 1.21x speedup**; that is an explanatory
counterfactual, not a forecast for further node counts.

Batch-8 idle-window share rises from roughly 5.6–5.8% to **10.7% on mesh** and
6.1–6.6% to **8.7% on road**. Offered inter-node attempts are **0.12% / 0.16%**
of attempts. These counts do not measure network bytes or exclude remote delay
as a cause of extra work. The sampled cost timers are aggregate estimates,
not an additive critical-path breakdown. Diagnostic/production paired time
ratios span **0.940–1.072**, so production timing drives the scaling conclusion.

## Decision and remaining gap

Keep batch 8 fixed and wait for the already queued independent comparison
22284706. There is no justification yet to enlarge the campaign or change
batch sizes based on one allocation. The preregistered two-allocation gate
is still incomplete. R2 remains conditional; R3 still needs the author's
decision. No solver settings or binaries changed, and no new jobs were
submitted during this analysis.

Relative to the previously measured one-node GAPBS references, two-node batch 8
remains **1.92–1.99x slower on mesh** and **2.70–3.34x slower on road**. Batch 32
reduces the mesh gap to 1.76–1.82x while increasing road cost. These reference
comparisons span allocations; both references are retained. This is early
positive scaling evidence for the optimized implementation, but it does not
close C6 or establish a strong distributed-performance paper. Road's growing
work remains the central issue if the second allocation confirms this pattern.

Evidence: [fresh two-node audit](onenode-data/r1-batch-two-node-22284705.json),
[all source-paired scaling/reference comparisons and accounting](onenode-data/r1-batch-two-node-scaling-22284705.json),
[distributed correctness audit](onenode-data/r1-batch-verification-22284699.json).
Frozen binaries, harness and configuration are described in the
[experiment record](ipdps27-r1-batch-scaling.md).
