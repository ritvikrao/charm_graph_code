# R0 one-node results: repeated expansions are a substantial target

2026-09-20. Jobs **22237670** (cn132) and **22237671** (cn112) completed,
exit 0, in 324 and 313 seconds. Actual allocated CPU time totals **22.649
CPU-hours**. The two eight-node jobs, 22237672/22237673, are still pending
for priority. R0 attribution remains incomplete until those results arrive;
R1 is not yet selected or implemented, R2 is conditional, and R3 awaits the
author's decision.

Both allocations used exclusive nodes, the same binaries, two training
sources, one warmup and two timed repetitions. ACIC uses 8 processes × 15
workers; GAPBS uses the frozen tuning (mesh: 128 threads, delta 4096; road:
64 threads, delta 32768). All ACIC arms have slack off. ACIC batches the two
sources into one launch; GAPBS launches each source separately. These are
diagnostic/training results, not a new final-acceptance panel.

## Validation and aggregation

Rechecking the raw logs passes **120 ACIC and 48 GAPBS full-distance digests**,
including warmups. All 72 diagnostic solves pass queue accounting; every
ACIC direct edge-attempt counter equals the existing production retirement
ledger, and the offered-update topology classes sum to the attempt count.
No missing cells, duplicate cells, rescue, stall or failed digest was found.

Tables take each source's median over its two timed repetitions, then the
median over the two sources. Ratios are formed on paired source medians
before aggregation. Allocations remain separate. No timing outliers are
removed. With only two repetitions, a single long run strongly affects its
source median.

Here, **edge means a stored directed edge** (an adjacency entry). An
undirected edge has two such entries. Counts are divided by reachable
stored edges or reachable vertices, as appropriate. An attempted relaxation
need not improve a distance, and successful improvements need not be final.

## Work and time

The candidate uses sharing plus tiles. ACIC attempts come from the
**uninstrumented** production ledger; GAPBS attempts require its diagnostic
build. Times below use production builds for both programs.

| Graph / allocation | ACIC seconds | GAPBS seconds | ACIC attempts / edge | GAPBS attempts / edge | Source-paired work ratio |
|---|---:|---:|---:|---:|---:|
| mesh26-z / A | 2.834 | 0.389 | 8.511 | 2.613 | 3.256 |
| mesh26-z / B | 2.996 | 0.608* | 8.657 | 2.613 | 3.311 |
| road-usa-z / A | 1.132 | 0.199 | 11.581 | 1.446 | 8.015 |
| road-usa-z / B | 1.143 | 0.242 | 11.247 | 1.444 | 7.794 |

*Allocation B has a GAPBS mesh outlier: source 41856222 takes 1.238302 s in
rep 1 versus 0.380011 s in rep 0. It remains in the table. Its cause is not
established. The ACIC/GAPBS paired time ratios are mesh 7.269 / 5.676 and
road 5.717 / 4.751 for A / B; the mesh B ratio is depressed by this outlier.

Sharing plus tiles remains substantially faster than sharing alone:

| Graph | Candidate / sharing-only time, A / B | Candidate / sharing-only attempts, A / B |
|---|---:|---:|
| mesh26-z | 0.573 / 0.570 | 1.051 / 1.033 |
| road-usa-z | 0.548 / 0.551 | 0.894 / 0.818 |

On one node, tiling adds only 3–5% mesh work and reduces road work by
11–18%, while improving time by 43–45%. The prior eight-node tiling/work
tradeoff therefore cannot be applied to one node. Preserve the beneficial
one-node placement while testing a processing-order change.

## Where the work goes

These are diagnostic-build quantities, not production wall-time partitions.

| Candidate metric | mesh A / B | road A / B |
|---|---:|---:|
| Expansions / reachable vertex | 8.435 / 8.615 | 10.998 / 11.047 |
| GAPBS expansions / reachable vertex | 2.613 / 2.613 | 1.383 / 1.382 |
| Stale entries / queue pops | 25.78% / 25.74% | 7.29% / 7.31% |
| Estimated queue time / solver work time | 44.83% / 43.46% | 49.75% / 48.95% |
| Solver work / (solve time × workers) | 88.21% / 86.79% | 88.44% / 88.37% |
| Worker CPU ns / attempt | 156.6 / 167.0 | 214.9 / 228.1 |
| GAPBS parallel-region CPU ns / attempt | 71.8 / 72.3 | 152.6 / 189.3 |

Repeated **expansions**, especially on road, are the larger work-efficiency
target. Stale entries already skip expansion and cannot directly account
for the excess edge scans. Tightening ordering might avoid intermediate
improvements and rescans; its effect is not established by counters alone.

Queue operations are a substantial cost as well. Their sampled times include
locking and overlap solver work; they are not an additive share of elapsed
time or a critical-path bound. Failed pop try-locks are about 0.10–0.25% of
probes, and CAS retry rates are below 0.002%. Low retry counts do not rule out
blocking push-lock costs or cache-coherence costs. At least 99.62% of offered
updates remain within a process; this is an offered-update count, not bytes
or an independently measured message-volume fraction.

The CPU-per-attempt comparison has unequal boundaries: ACIC includes worker
runtime/waiting, whereas GAPBS covers its parallel region. It suggests both
work amount and execution cost matter, particularly on mesh; it does not
isolate a pure runtime tax. Reducing the queue's constant cost alone offers
no demonstrated route across the current several-fold time gap.

## Perturbation, controls and next decision

Candidate diagnostic/production paired timing ratios are **1.043 / 1.076**
on mesh and **1.009 / 1.061** on road. Their paired attempt ratios are
**0.990 / 0.994** and **0.975 / 1.000**, respectively. Individual source work
ratios range from 0.933 to 1.018. Sharing-only diagnostic work also changes
by up to about 7% at the source-median level. Keep production work and time
as the intervention's primary outcomes.

The repeated ACIC production control has a separate mesh outlier in A:
source 41856222, rep 0, takes **6.667772 s**, versus **2.574667 s** in rep 1,
and attempts per edge rise from **7.315** to **12.888**. Some of its slowdown
is increased algorithmic work, so it cannot be labeled pure machine noise.
Its source-median control/candidate ratios are 1.824 in time and 1.386 in
work. Across the other seven graph/allocation/source cells, control/candidate
timing ratios range from 0.941 to 1.043. No outlier is silently excluded
from aggregate tables or ratios.

The leading R1 hypothesis remains tighter selection across process-local
producer queues: the current queue tries its own producer bin first, even
if another bin has a lower original bucket. The one-node results support
testing that mechanism but do not prove it causes the observed rework.
Before implementation, incorporate the pending eight-node work/cost
measurements and record the expected gain and a disconfirming result.
An intervention must reduce production work **and** time in independent
allocations while retaining useful parallelism; fewer attempts alone do
not pass. Include enough repetitions to expose the observed timing/work
variability. No additional jobs were submitted for this analysis.

Reproduce the raw-log validation and source-paired tables with:

```sh
python3 benchmarks/summarize_r0.py /u/rao1/.tmp/ipdps27-onenode \
  22237670 22237671 --output design/onenode-data/r0-one-node-22237670.json
```

[Machine-readable results](onenode-data/r0-one-node-22237670.json) retain
every warmup and timed metric, both allocations, build manifests, GAPBS
settings, per-source summaries and raw-log SHA-256 hashes.
