# R1 one-node audit: valid runs, substantial coordination cost

2026-09-21. **Both one-node R1 jobs are complete and valid.** Job 22281605
ran on cn130 and 22281606 on cn133; both exited 0 after 284 seconds. Together
they used 20.196 allocated CPU-hours. Eight-node jobs 22281608/22281609 remain
pending. No replacement or extra performance jobs were submitted for this audit.

## Correctness and experiment integrity

Repaired verification **22281603 passed all 88 solves** in 39 seconds on
cn007, using 0.173 allocated CPU-hours. Raw serial and parallel digests match
for every solve. All 44 diagnostic cases pass queue, edge-retirement and
destination accounting, including all eight dense cases with actual
sender-filter drops. The previous diagnostic failure is resolved.
[Verification audit](onenode-data/r1-verification-22281603.json).

The performance audit independently checks **160 complete solves** across
80 launches: two graphs × five arms × two sources × warmup plus three timed
repetitions, in each of two allocations. Every full-distance digest matches
the reference; all 32 diagnostic solves pass the three-way work accounting.
Raw solve times agree with the result rows. No missing/duplicate cells,
reordered sources, stalls, rescues, truncation, aborts or failed runs were found.

Every launch has the expected eight processes, 120 workers, sharing on,
reader tiling on and slack off. Queue-policy logs match local/nearest.
Recorded binary hashes match the immutable binaries and submitted v2
configuration; the diagnostic arm uses the repaired
`acic_r1_priority_diag_v2`. Extracted source logs match their original
batched-launch output byte-for-byte, and stored diagnostic counters and
timers match the raw records.

All launches emit the existing LCI warning that the UCX memory-registration
cache is unavailable and execution continues without caching. It appears
24 times per launch in all arms of both new jobs and both earlier one-node
R0 allocations. No other runtime warning/error was found. This is a shared
runtime condition, not a newly introduced R1 failure; its absolute
performance effect was not measured in this experiment.

## Preliminary performance caveat

These are training results. First take the median over the three timed
repetitions for each source, then aggregate sources. Ratios are computed
on paired source medians before aggregation. All observations remain in
the data, and allocations are kept separate.

| Graph / allocation | Frozen R0 time (s) | Nearest time (s) | Frozen R0 attempts / edge | Nearest attempts / edge | Paired nearest/R0 time |
|---|---:|---:|---:|---:|---:|
| mesh26 / A | 2.769 | 2.425 | 8.483 | 1.427 | 0.887 |
| mesh26 / B | 2.801 | 2.414 | 8.617 | 1.428 | 0.873 |
| road / A | 1.074 | 1.175 | 11.043 | 2.186 | 1.094 |
| road / B | 1.137 | 1.176 | 11.337 | 2.177 | 1.034 |

Here an edge means a stored directed adjacency entry. Production edge
attempts fall about **83% on mesh and 80% on road**, confirming that the
ordering change strongly affects repeated work. Time improves only 11–13%
on mesh and is 3–9% worse on road versus the frozen R0 binary. The same-binary
comparison also lacks a reproducible road gain:

| Graph | Nearest/local time, A / B | Nearest/repeated-local-control time, A / B |
|---|---:|---:|
| mesh26 | 0.831 / 0.878 | 0.868 / 0.830 |
| road | 0.865 / 1.034 | 1.016 / 1.043 |

Road allocation A has appreciable baseline variability. For source 1294456,
the local repetitions are 1.486453, 1.097058 and 1.482198 s; the repeated
local control is 1.125328, 1.503525 and 1.072701 s. The slower repetitions
also scan more edges (about 17 attempts/edge versus 13). The control/local
source-median time ratio is 0.759. Thus nearest's apparent win over the
first local arm in A does not survive the repeated control or allocation B.
No measurements were removed as outliers.

Nearest diagnostics show solver work costs of **711–713 ns/attempt** on
mesh and **1,014–1,019 ns/attempt** on road. Estimated queue operations
consume **71–75% of solver work**; failed pop try-locks are **27–31% of
queue probes**. The earlier R0 local-policy diagnostics, from different
allocations, had much lower costs and retry fractions. This supports the
coordination-cost concern, consistent with workers contending for the same
preferred queue. Queue timers include hint scans and locking and overlap
solver work; they are not a critical-path partition.

The diagnostic/production time ratios are mesh 1.021 / 1.018 and road
1.007 / 0.989. Corresponding work ratios are 1.003 / 1.000 and 0.975 /
0.969. Production time and work remain the decision metrics.

**Status:** the jobs are usable, and the instrumentation gate passes.
The one-node performance gate is not satisfied on both graphs. Finish the
already queued eight-node comparisons before the complete R1 judgment;
do not advance to R2 or R3 from these one-node results.

[Machine-readable results](onenode-data/r1-one-node-22281605.json) retain
all warmup/timed rows, allocation manifests, source-paired summaries and
raw-log hashes. Reproduce the audit with:

```sh
python3 benchmarks/check_priority_runs.py /u/rao1/.tmp/ipdps27-onenode \
  22281605 22281606 \
  --config /u/rao1/.tmp/ipdps27-onenode/configs/r1-priority-v2.json \
  --output design/onenode-data/r1-one-node-22281605.json
```
