# R1 batching: completed two-node comparison

2026-09-21. **Both allocations completed and pass validation.** Batch 8
reproduces a useful mesh speedup and a modest aggregate road speedup over
optimized one-node execution. Road scaling is sensitive to the source and
extra work remains its main measured limitation. The author requested a
status/plan update and **no further submissions**; no jobs are active or queued.

## Validation and accounting

| Job | Role | Hosts | Elapsed | Allocated CPU-hours |
|---|---|---|---:|---:|
| 22284699 | Distributed correctness | cn064, cn066 | 120 s | 1.067 |
| 22284705 | Two-node A | cn016, cn088 | 345 s | 24.533 |
| 22284706 | Two-node B | cn076, cn108 | 343 s | 24.391 |

All jobs completed with exit 0, using **49.991 allocated CPU-hours** against
an 88-SU reservation cap. The two performance allocations used disjoint node
sets. A shares cn016 with the earlier one-node A allocation; all one/two-node
comparisons retain both one-node allocations independently.

A fresh audit validates all **256 performance solves**, **96 diagnostic
records** and **128 launches**. It agrees exactly with both audits produced
inside the jobs. Reference digests, source order, binary/configuration hashes,
complete matrices, 16 processes x 15 workers, queue conservation and edge/topology
accounting all pass. There are no new runtime errors, stalls or rescues; the
previously documented UCX registration-cache warnings remain.

The correctness gate's preserved raw-log audit validates **224 serial/parallel
solves**, **112 diagnostic accounting records**, and **90 connected diagnostic
solves with positive inter-node work**. Thus the completed stage contains
**480 validated solves**. No solver source, binary or flags changed between
allocations. Warmups are validated and excluded from timing medians.

## Scaling from the optimized baseline

Two training sources per graph, three timed repetitions per source. Take
source medians first, then pair those medians before aggregating ratios.
Speedup is the median of one-node/two-node source time ratios; its range below
retains comparisons with both one-node allocations.

| Graph / two-node allocation | One-node batch 8, A / B | Two-node batch 8 | Speedup vs one-node A / B | Two/one edge-work growth, A / B |
|---|---:|---:|---:|---:|
| mesh26-z A | 1.116 / 1.150 s | 0.774 s | 1.448 / 1.488x | 1.230 / 1.228x |
| mesh26-z B | 1.116 / 1.150 s | 0.773 s | 1.451 / 1.491x | 1.222 / 1.220x |
| road-usa-z A | 0.748 / 0.729 s | 0.651 s | 1.151 / 1.123x | 1.648 / 1.650x |
| road-usa-z B | 0.748 / 0.729 s | 0.680 s | 1.104 / 1.079x | 1.720 / 1.724x |

Mesh reproduces **1.45–1.49x speedup**, or 72–75% parallel efficiency when
nodes double. Road reproduces only **1.08–1.15x**, or 54–58% efficiency.
Batch-8 attempts/edge rise from 1.533–1.536 to **1.885 / 1.872** on mesh and
from 2.929–2.932 to **4.845 / 5.068** on road. Extra work is **22–23% / 65–72%**
respectively. All source medians improve on mesh; road has one observed
regression that must remain visible.

For road source **1294456**, two-node B takes **0.732179 s**, versus
**0.715004 s** in one-node B: **2.4% slower**. The repeated two-node control
is 0.707216 s, only 1.1% faster than that one-node result. Primary timed values
are 0.732179 / 0.733927 / 0.672456 s; control values are 0.707216 / 0.755122 /
0.681030 s. This is within observed variation and does not prove a stable
regression, but it also does not establish a reliable gain for this source.
It corrects the first allocation's provisional observation that every source
median improved.

Repeated batch-8 controls take mesh **0.745 / 0.754 s** and road **0.650 /
0.678 s**. Paired control/candidate ratios are mesh **0.965 / 0.978** and road
**1.001 / 1.000**. The largest source-level differences are 5.2% on mesh and
3.4% on road. The graph-level gains exceed these observed control differences,
but the weaker road source does not. Keep primary arms, controls and allocations
separate; do not select the faster copy or remove a source.

## Batching still helps; larger batches are not a general improvement

| Graph / allocation | Frozen R0 | Nearest / 1 | Batch 8 | Batch 32 |
|---|---:|---:|---:|---:|
| mesh26-z A | 1.759 s | 1.752 s | 0.774 s | 0.708 s |
| mesh26-z B | 1.886 s | 1.760 s | 0.773 s | 0.763 s |
| road-usa-z A | 0.923 s | 1.117 s | 0.651 s | 0.700 s |
| road-usa-z B | 0.937 s | 1.021 s | 0.680 s | 0.723 s |

Against same-binary nearest/1, batch 8 reduces paired time **56% on mesh**
and **33–42% on road**. Against frozen R0 the reductions are **56–59% / 28–30%**.
The queue-cost benefit therefore survives crossing a node boundary in both
allocations. Unbatched nearest remains 9–21% slower than frozen R0 on road.

Batch 32 performs **35–37% more mesh work and 68–69% more road work** than
batch 8. Its mesh time advantage drops from 8.6% in A to 2.0% in B; relative
to the repeated batch-8 control, it is 5.2% faster in A but 0.4% slower in B.
Mesh source 22442342 is itself 3.3% slower with batch 32 in B. Road is
6.6–7.5% slower with batch 32 in both allocations. Retain batch 8 as the common
candidate; these results do not justify a graph-name policy or another size sweep.

## Why road scaling is weak

The matched diagnostics confirm reduced per-attempt queue cost:

| Diagnostic metric | Mesh nearest/1, A / B | Mesh batch 8, A / B | Road nearest/1, A / B | Road batch 8, A / B |
|---|---:|---:|---:|---:|
| Solver work ns / attempt | 774 / 852 | 318 / 308 | 1,024 / 1,148 | 457 / 452 |
| Estimated queue ns / attempt | 551 / 603 | 183 / 176 | 765 / 852 | 294 / 288 |
| Queue share of solver work | 71.2% / 70.8% | 57.4% / 57.3% | 74.6% / 74.5% | 64.6% / 63.8% |
| Consumed entries per removal call | 0.89 / 0.88 | 5.35 / 5.37 | 0.90 / 0.89 | 5.39 / 5.39 |

For batch 8, one-node solver cost was **304–305 ns/attempt on mesh** and
**464–474 on road**. Two-node cost stays broadly similar. Road queue cost
also stays near its one-node 293–297 ns/attempt. Thus the major measured road
scaling loss is the increase in operation count, not a large increase in
per-attempt cost. At constant cost and perfect balance, 1.65–1.72x work on twice
as many workers would yield only **1.16–1.21x speedup**. This is an explanatory
counterfactual, not a forecast for four/eight nodes.

Batch-8 idle-window shares are **10.7% on mesh** and **8.7–8.9% on road**, above
one-node levels. Offered inter-node attempts are about **0.12% / 0.16%**;
these are not network bytes or evidence against remote-delay effects on work.
Sampled timers are aggregate estimates, not critical-path decompositions.
The measurements do not separate the effects of more process-local priority
queues, changed partitioning, remote-update delay and admission/controller timing.

Diagnostic/production time ratios across all arms span **0.940–1.112**;
work ratios span **0.932–1.036**. In particular, nearest-1 road diagnostics
are 11.2% slower in B, and batch-32 road diagnostics perform 6.8% less work.
Production times and production work ledgers drive the decision.

## Decision and remaining gap

The planned **two-allocation comparison is complete**. Positive graph-level
scaling reproduces for both graphs, with mesh clearly stronger. This meets
the limited aggregate direction of the training hypothesis; it does not
establish robust per-source road scaling or a competitive distributed solver.
The small road gain, one non-improving source and 65–72% work growth are material
limitations, not a reason to relabel this stage a failure of correctness.

Two-node batch 8 remains **1.92–1.99x slower than one-node GAPBS on mesh** and
**2.70–3.48x slower on road**, retaining both previous GAPBS allocations.
These are cross-allocation comparisons. Batch 32's mesh gap is 1.76–1.96x;
it worsens road performance. The existing eight-node results are for the older
unbatched build; batched eight-node scaling and C6 remain unmeasured.

**Submissions are paused at the author's request.** No jobs were submitted,
and Slurm showed no active/queued jobs at this review. Preserve the batch-8
candidate and completed data. The recommended next research question is what
causes road's distributed work growth, before spending on broad scaling or
secondary RMAT cleanup. The [revised plan](ipdps27-onenode-gap.md) specifies a
possible fixed-total-layout diagnostic if the author later resumes experiments.
R2 remains deferred; R3 has not been authorized. No solver defaults change.

The paper now has reproducible local execution gains and a modest two-node
scaling result. It still lacks evidence for a strong large-scale/high-diameter
performance claim. Conventional local batching can be credited infrastructure;
the distributed contribution and its advantage over feasible alternatives
still need to be demonstrated.

## Evidence

[Complete two-node audit](onenode-data/r1-batch-two-node-complete-22284705.json),
[all source-paired scaling/reference comparisons and job accounting](onenode-data/r1-batch-two-node-complete-scaling-22284705.json),
[distributed correctness audit](onenode-data/r1-batch-verification-22284699.json),
and [frozen experiment record](ipdps27-r1-batch-scaling.md).
The earlier first-allocation audit and scaling artifacts remain preserved;
this report supersedes that allocation's provisional interpretation.
