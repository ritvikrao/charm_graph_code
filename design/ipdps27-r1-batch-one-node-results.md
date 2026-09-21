# R1 batching: one-node result and next decision

2026-09-21. **The batching hypothesis passes its one-node test.** Both tested
sizes convert R1's reduction in redundant work into a substantial runtime
improvement on both training graphs and both allocations. Carry **batch 8**
forward as a single setting for the next scaling experiment; retain batch 32
as a fixed comparison. This is a training decision, not a new default or a
final acceptance result. Distributed scaling remains unmeasured for this build.

## Completion and correctness

| Job | Role | Host | Elapsed | Result |
|---|---|---|---|---|
| 22282643 | Small-graph serial verification | cn008 | 80 s | 224/224 solves pass |
| 22282647 | One-node A | cn016 | 491 s | 144 ACIC + 32 GAPBS solves pass |
| 22282652 | One-node B | cn053 | 501 s | 144 ACIC + 32 GAPBS solves pass |

All jobs completed with exit 0; allocated CPU time totals **35.627 hours**
against the 52-SU reservation cap. All **576 solves** were rechecked against
raw logs: serial/parallel digests for the 224 verification solves and independent
reference digests for the 352 benchmark solves. The 112 verification diagnostic
records and 64 ACIC performance diagnostic records pass queue conservation,
direct-versus-production edge accounting and offered-update topology sums.
All 32 GAPBS diagnostic records pass their queue identities. The verification
includes 24 dense diagnostic cases with sender filtering and actual range
extensions in the narrow-width cases.

The performance audit checks the full cell matrix, 144 ACIC launches, source
order, frozen binary/configuration hashes, 8 processes x 15 workers, effective
queue policy and batch size, sharing, tiling and slack settings. No new runtime
errors, stalls or rescues were found. The previously documented UCX registration
cache warnings remain in the ACIC logs. Both GAPBS matrices are complete with
their frozen binaries, thread counts, delta values and matching allocation hosts.

## Production times

Seconds below are medians of the two source medians; each source has three
timed repetitions. Warmups are validated but excluded. Ratios elsewhere are
formed per source before aggregation, so they need not equal ratios of the
displayed times. All arms and outliers are retained.

| Graph / allocation | Frozen R0 | Nearest / 1 | Nearest / 8 | Nearest / 32 | GAPBS |
|---|---:|---:|---:|---:|---:|
| mesh26-z A | 2.823 | 2.444 | 1.116 | 0.963 | 0.388 |
| mesh26-z B | 2.965 | 2.845 | 1.150 | 0.971 | 0.402 |
| road-usa-z A | 1.115 | 1.162 | 0.748 | 0.775 | 0.196 |
| road-usa-z B | 1.124 | 1.361 | 0.729 | 0.776 | 0.243 |

Batch 8 / nearest-1 paired time ratios are **0.456 / 0.403** on mesh and
**0.647 / 0.538** on road. Against frozen R0 they are **0.396 / 0.387** on
mesh and **0.671 / 0.648** on road: approximately **60–61% less time on mesh
and 33–35% less on road**. Every source median improves against both copies
of the nearest-1 control and against frozen R0.

Batch 32 / nearest-1 ratios are **0.392 / 0.340** on mesh and **0.671 / 0.576**
on road. Against frozen R0 they are **0.341 / 0.327** and **0.696 / 0.691**.
Thus the best observed mesh setting is about three times faster than frozen R0.
The graph-specific best settings are descriptive, not an adopted graph-name policy.

Nearest-1 repeated-control time ratios are mesh **0.995 / 1.039**, road
**1.001 / 0.953**; the largest source-level discrepancy is road B's control
being 9.4% faster. The batching gains exceed that variation. New nearest-1
versus frozen nearest is within about 2% at the paired aggregate level,
although individual road B sources vary more. New local-1 versus frozen R0
is 1.1–8.6% slower across graph/allocations; this comparison has no repeated
local control in the new experiment, so it cannot establish absence of a
default-path regression. Keep the intervention opt-in and retain the final
regression gate.

GAPBS mesh A source 41856222 has a retained 1.594-second repetition alongside
0.374 and 0.378 seconds; the specified median uses 0.378. Road GAPBS is slower
in B than A. Do not merge allocations or choose the more favorable reference.
Batch diagnostic/production paired timing ratios span **0.979–1.068**;
diagnostic work differs by at most about 2.1% at the paired aggregate level.
Production timing drives the decision.

## Why it helps, and where larger batches lose

An edge here means one stored directed adjacency entry. Attempt counts come
from the validated production ledger; GAPBS counts use its diagnostic build.

| Graph / allocation | Frozen R0 attempts/edge | Nearest / 1 | Nearest / 8 | Nearest / 32 | GAPBS |
|---|---:|---:|---:|---:|---:|
| mesh26-z A | 8.590 | 1.431 | 1.533 | 1.861 | 2.615 |
| mesh26-z B | 8.688 | 1.419 | 1.536 | 1.877 | 2.620 |
| road-usa-z A | 11.361 | 2.180 | 2.932 | 4.555 | 1.445 |
| road-usa-z B | 11.273 | 2.209 | 2.929 | 4.526 | 1.445 |

Batch 8 raises attempts by **7–8% on mesh and 33–34% on road** relative to
nearest-1, while reducing time. This is evidence that reduced coordination
cost more than compensates for weaker priority ordering. It is not evidence
that minimizing edge attempts alone minimizes time, or that Bellman–Ford-style
relaxation imposes the previous runtime floor.

Batch 32 adds **21–22% mesh work and 54–55% road work** relative to batch 8.
Its paired mesh time is **14–16% lower**, but road time is **3.6–6.6% higher**.
The small road timing difference overlaps some run/control variation; the
large and reproducible road work increase is the stronger reason to prefer
batch 8 for a conservative scaling test. Larger local batches delay newly
arriving nearer work; operation count can erase the gain per operation.

| Diagnostic metric | Mesh batch 8, A / B | Mesh batch 32, A / B | Road batch 8, A / B | Road batch 32, A / B |
|---|---:|---:|---:|---:|
| Consumed entries per removal call | 6.30 / 6.37 | 15.97 / 16.31 | 5.98 / 5.89 | 14.94 / 14.73 |
| Solver work ns / attempt | 305 / 304 | 215 / 213 | 474 / 464 | 315 / 301 |
| Estimated queue ns / attempt | 172 / 172 | 99 / 99 | 297 / 293 | 165 / 157 |
| Estimated queue share of solver work | 56.4% / 56.7% | 45.7% / 45.7% | 62.7% / 63.1% | 51.8% / 52.2% |
| Failed try-locks / queue probes | 14.1% / 14.7% | 7.8% / 8.0% | 13.4% / 12.6% | 7.8% / 7.9% |

Removal calls include unsuccessful calls, so the first row is not the average
size conditional on a successful batch. Prior nearest-1 diagnostics measured
711–713 ns/attempt on mesh and 1,014–1,019 on road, with 71–75% queue share and
27–31% failed try-lock probes. That comparison spans allocations: the new jobs
do not contain a nearest-1 diagnostic arm. The directly matched comparison
between batches 8 and 32 nevertheless shows the expected cost reduction.
Sampled timers are aggregate estimates, not additive critical-path fractions.

Only **36–44%** of remaining sampled queue time is removal; insertion now
accounts for most of it. CAS failure fractions remain below 0.03%, and measured
idle-window shares are about 5.6–6.6%. These observations identify remaining
cost, but do not authorize an unbounded sequence of further queue changes.

## Remaining gap and decision

Even the graph-specific best setting remains slower than matched one-node
GAPBS: mesh batch 32 is **2.472 / 2.411 times** slower, and road batch 8 is
**3.852 / 3.041 times** slower. Mesh batch 8 is **2.868 / 2.857 times** slower.
Mesh batch 32 already attempts fewer edges than GAPBS; further work reduction
alone cannot explain away the remaining execution-cost gap. Road batch 8
attempts about twice GAPBS's work, so both work and cost still matter there.

**Proceed to a bounded scaling test of the existing batching build**, with
batch 8 fixed for both graphs and batch 32 retained as an ablation. First
compare one versus two nodes on the same training sources, counts and binary;
use two independent two-node allocations, repeated candidate controls and
work-cost diagnostics. The existing one-node allocations supply the matched
source baselines; show both allocations separately. Measure absolute time,
paired speedup, edge-work growth and per-attempt cost. If two-node time fails
to improve beyond control variation, inspect the cause before spending on
larger allocations. Otherwise proceed toward the eight-node scaling/C6 test
when scheduling permits. Do not retune batch size at each node count.

The original eight-node nearest-1 jobs **22281608/22281609 were pending when
this analysis was completed**. They subsequently finished: see the
[eight-node sanity check](ipdps27-r1-eight-node-check.md). They measure R1
ordering at scale, but cannot establish scaling for the batching implementation.
After receiving this results update, the author authorized the
[bounded two-node batching experiment](ipdps27-r1-batch-scaling.md).

R1 now has a credible one-node candidate. It has **not yet passed its distributed
performance gate**. Prioritize establishing that scaling before the small RMAT
auto-mode issue; R2 remains conditional on a candidate worth final acceptance.
R3 still requires the author's decision. Defaults and C6 are unchanged.

For the paper, this supports treating conventional local batching as useful
infrastructure. It does not establish the proposed distributed contribution.
Any strong-scaling claim must start at the new optimized one-node times, not
the 2.8–3.0-second mesh baseline. A successful mechanism ablation and competitive
distributed results on larger high-diameter graphs are still needed.

## Reproducibility

The [implementation record](ipdps27-r1-batch-progress.md) identifies frozen
binaries, configuration and harness. Raw logs are under
`/u/rao1/.tmp/ipdps27-onenode/logs`. The strict ACIC audit can be rerun with:

```sh
python3 benchmarks/check_priority_runs.py /u/rao1/.tmp/ipdps27-onenode \
  22282647 22282652 \
  --config /u/rao1/.tmp/ipdps27-onenode/configs/r1-batch.json \
  --output /tmp/r1-batch-one-node.json
```

Committed evidence contains per-source/repetition metrics and raw hashes:
[ACIC audit](onenode-data/r1-batch-one-node-22282647.json),
[GAPBS audit](onenode-data/r1-batch-gap-22282647.json),
[serial verification audit](onenode-data/r1-batch-verification-22282643.json),
and [Slurm accounting and reference-audit script provenance](onenode-data/r1-batch-accounting-22282643.json).
The preserved reference-audit script checks both complete GAPBS matrices and
the exact 56-log verification matrix against the original raw files.
