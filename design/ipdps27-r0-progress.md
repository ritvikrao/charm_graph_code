# R0: redundant work before another optimization

2026-09-20. The author authorized R0–R2 and requested a results update before
deciding whether to start R3. **R3 is not authorized by this continuation.**
The priority order remains redundant-work diagnosis, one selected intervention,
then conditional dense-mode cleanup.

## Status

All four full-node R0 allocations completed and their raw results passed
validation: 288 solves and 120 diagnostic queue identities. R0 supports a
bounded ordering counterfactual; it does not establish full runtime or
critical-path attribution. The [R1 decision](ipdps27-r1-decision.md) selects
process-wide shared-queue priority. The [R1 implementation](ipdps27-r1-progress.md)
is built and locally checked; compute-node verification and its dependent
performance comparisons are queued. R2 remains conditional on a promising
R1. No defaults changed.

[The one-node analysis](ipdps27-r0-one-node-results.md) confirms substantial
repeated expansions in two independent allocations: production ACIC makes
8.51–8.66 attempts per stored directed edge on mesh26 and 11.25–11.58 on
road, versus GAPBS's diagnostic 2.61 and 1.44–1.45. Queue operations also
account for an estimated 43–50% of measured solver work. Tiling improves
one-node time by 43–45%; it adds little mesh work and reduces road work.
The report retains an ACIC control outlier and a GAPBS timing outlier.
All 168 solves (120 ACIC, 48 GAPBS, including warmups) passed full digests;
all 72 diagnostic solves passed queue accounting. The two completed
allocations consumed 22.649 allocated CPU-hours.

The whole-node scheduler initially estimated several hours of waiting. The
original pending reservations 22237631–22237634 were cancelled without running
and replaced with batched-source jobs. Each arm now solves both training
sources after one CSR load, with identical batching for production, repeated
control and diagnostic variants. This cuts launch/read overhead and reduces
the aggregate wall-time-limit exposure from 537.6 to **256 SU**.

| Job | Role | Wall-time limit | Limit in CPU-hours | Status |
|---|---|---|---:|---|
| 22237670 | One node, allocation A, plus full-node GAPBS | 12 minutes | 25.6 | Completed, cn132 |
| 22237671 | One node, allocation B, plus full-node GAPBS | 12 minutes | 25.6 | Completed, cn112 |
| 22237672 | Eight nodes, allocation A | 6 minutes | 102.4 | Completed, 220 s |
| 22237673 | Eight nodes, allocation B | 6 minutes | 102.4 | Completed, 202 s |

Graphs: `mesh26-z`, `road-usa-z`. Two training sources, warmup and two timed
repetitions. ACIC uses eight processes of fifteen workers per physical node.
Arms: sharing alone, sharing plus reader tiles, a repeated tiled production
control, and the corresponding two diagnostic variants. All have slack off.
The panel uses a frozen harness under
`/u/rao1/.tmp/ipdps27-onenode/build/r0-harness`, configuration
`configs/r0.json`, and immutable `acic_r0_control` / `acic_r0_diag_v3` binaries.
Full manifests and source inventories accompany each build.

## What is measured

`work_cost.h` adds per-worker counters under `ACIC_WORK_COST`, without the
older diagnostic's per-vertex atomic arrival/lead arrays. It records scanned
edges, expansions, successful improvements, vertex queue pushes/pops, stale
pops, CAS attempts/failures, queue probes and unsuccessful try-locks. Shared
queue push/pop calls are sampled once per 1,024 calls using TSC ticks.
The `work-cost` build also enables the existing aggregate work/send timers.

Worker CPU time uses `CLOCK_THREAD_CPUTIME_ID`. ACIC's window includes worker
runtime and waiting activity; GAPBS's window covers its parallel region and
excludes serial distance/frontier allocation. Sampled queue time includes
locking, is part of work time, and is an estimate. These measurements are
not independent pieces of wall time. No critical-path attribution or L4
trigger follows merely from a large round count.

The intra-process/intra-node/inter-node counters classify offered edge
updates **before** transport aggregation or absorption. They are not measured
network bytes. The final interpretation must preserve this distinction.

The GAPBS diagnostic adds counters to a private copy of the pinned upstream
`sssp.cc`; bucket fusion, scheduling and distance tests are retained. The
builder checks every insertion's context, records source/binary hashes and
keeps the generated source. The upstream checkout and production `gap_sssp`
binary are unchanged. The report checks full digests and counter identities;
production/diagnostic A/Bs quantify instrumentation perturbation.

## Counter validation

Verification job **22237625 passed**: 32 ACIC solves across sharing/tiling
on/off, two processes with four workers each, four successive sources including
an isolated vertex and a repeated source, plus six GAPBS solves. Distances
match serial verification / the production GAPBS digest. All diagnostic queue
identities pass: pushes equal pops, and expansions plus stale pops equal pops.
The existing concurrent work-queue test also passes with instrumentation.
Six report-parser tests pass, including malformed/incomplete record rejection.

An earlier verification, 22237563, caught a diagnostic startup bug. The timer
broadcast can arrive after the injected source, erasing its queue counter.
The solver returned correct distances; the counter identity failed. Version
v3 starts counters idempotently on timer pickup or first work and resets the
start state at the existing source-reset barrier. No performance panel used
the failing v2 build. The local login-node smoke could not initialize LCI's
OFI device; correctness verification therefore ran on compute nodes.

## A production measurement requiring no new instrumentation

For the non-lazy path, existing logs already determine the exact edge scans:

```
edge_attempts = printed_wasted + V - 1
              + send_filtered + absorbed + batch_folded
```

Every scanned edge is either filtered before creating an update, absorbed,
folded, or retired at the receiver. The existing `Wasted updates` label prints
the retired count minus all V; subtract one for the injected source, which
has no incoming edge. Use graph V, not reachable V, in this identity. The
formula is deliberately rejected for lazy-heavy runs. It matches the new
direct counter on the correctness fixtures, including the isolated source.

Using four held-out sources and three repetitions in the prior **slack-off
production candidate** jobs 22222843/22222842 gives:

| Graph | Scans / arc, one node | Scans / arc, eight nodes | Source-paired work growth |
|---|---:|---:|---:|
| mesh24-z | 7.778 | 19.280 | 2.511 |
| mesh26-z | 8.854 | 22.657 | 2.532 |
| road-usa-z | 10.500 | 49.652 | 4.699 |
| road-usa-w4-z | 10.604 | 50.088 | 4.765 |

Values are medians of per-source medians. This recovers work counts from the
exact measured candidate, overcoming the earlier slack-on D0 mismatch.
Independent reader pilot 22218785 corroborates the scale of the work: tiled
mesh26 scans/arc range 17.98–26.73 and tiled road 40.63–66.16. Sharing alone
in that pilot has ranges 7.64–9.62 and 18.10–28.61, respectively. Tiling
increases throughput but also increases work; occupancy alone is insufficient.

Two short structural GAPBS jobs, **22237693** (cn002) and **22237694** (cn004),
completed while whole-node jobs waited. These use shared nodes, 64 reserved
cores and delta 32768, the frozen tuned road setting. Their times are **not
acceptance measurements**. All 24 production/diagnostic solves passed digests,
and diagnostic queue identities passed. For the two training sources:

| Source | GAPBS scans / arc, A / B | GAPBS expansions / reachable vertex, A / B |
|---|---:|---:|
| 5620086 | 1.4520 / 1.4526 | 1.3884 / 1.3888 |
| 1294456 | 1.4423 / 1.4427 | 1.3803 / 1.3807 |

Per-source diagnostic/production timing ratios range from 1.06 to 1.24 in
these shared-node samples. This is why counters and production timings must
remain separate. These jobs add at most 6.4 SU of reservation exposure;
actual combined allocated CPU time is 0.64 hours.

Data and job IDs are in [r0-initial-work.json](onenode-data/r0-initial-work.json).
The main campaign remains `/u/rao1/.tmp/ipdps27-onenode` with graph files in
the work filesystem via compatibility links.

## Decision recorded after the complete panel

The leading R1 hypothesis is tighter priority selection across a process's
shared producer bins: the current queue tries its own bin before peers even
when peers hold lower-priority-key work. The observed excess scans provide
substantial headroom, but they do not show how much a new queue policy can
remove or whether its synchronization cost will outweigh that reduction.

The complete panel confirms eight/one work growth of 2.58× on mesh and
4.5× on road, with broadly similar measured solver cost per attempt.
The [decision and bounded comparison](ipdps27-r1-decision.md) records the
prediction and disconfirming result before implementation. Any intervention
must reduce production work and time reproducibly. R2 must not displace this
analysis. Report the resulting R0–R2 evidence to the author and wait for the
R3 decision; do not launch R3 acceptance jobs automatically.
