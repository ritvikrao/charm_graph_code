# R1 follow-up: amortize process-queue removal

2026-09-21, recorded before implementation. The author explicitly authorized
this bounded follow-up to R1. It extends the original stop-after-one-intervention
rule; it does not authorize R3 or a general parameter search. Focus on one node
without waiting for the two already queued eight-node R1 jobs.

Nearest priority reduces mesh attempts from about 8.5 to 1.43 per stored
directed edge, and road attempts from about 11.2 to 2.18, but only modestly
improves mesh time and does not reliably improve road time against frozen R0.
Removal accounts for 73.4–74.3% of sampled queue time; queue time is about
71–75% of measured solver work. These are aggregate sampled costs, not
critical-path fractions. See [the validated results](ipdps27-r1-one-node-check.md).

Add opt-in `--process-queue-batch N`, default 1, bounded at 64. After choosing
a producer bin, remove up to N admitted entries under one lock and publish
its new head once. Expand after unlocking, drain the private batch before
yielding, and retain the existing 100-entry progress bound. Preserve original
bucket order, distance CAS and each entry's outstanding histogram charge.
Recheck admission before expansion in case an inline delivery applies a new
controller generation; return newly ineligible entries without creating a
second charge. A private batch is still outstanding work, even if the shared
queue appears empty. No asynchronous buffer or new termination rule is added.

Prediction: fewer scans and lock acquisitions per consumed entry will lower
queue cost per attempt and production time. Batching may increase redundant
edge attempts by delaying new nearer entries, and may hold a selected bin's
lock longer. Accept only a reproducible net timing benefit, not fewer queue
calls alone. Compare sizes 8 and 32, fixed in advance, against size 1; do not
tune additional sizes from these results. Keep reader tiles, sharing auto,
slack off, two fixed training sources and three timed repetitions per source.

Two independent one-node allocations compare frozen R0, frozen R1 nearest,
new local/1, new nearest/1, nearest/8, nearest/32, repeated nearest/1, and
diagnostic nearest/8 and /32. Also run the existing GAPBS production and
diagnostic references in each allocation. Rotate ACIC arm order and preserve
warmups, raw logs, binary hashes and all outcomes. Full serial correctness on
small sparse/dense graphs, repeated/isolated sources, admission boundaries,
source resets and diagnostic conservation must precede performance through
an `afterok` dependency. Standalone queue tests cover batched admission and
concurrent exactly-once removal. Two 12-minute full-node reservations plus a
three-minute 16-core verification reserve at most 52 SU within R0–R2's
2,000-SU cap. No new multi-node reservations are needed for this experiment.

If both sizes fail to improve time beyond repeated-control variability, stop
this batching hypothesis and report the result. If one is promising, report
its work/cost tradeoff and recommend the next scaling test before expanding
the campaign. R2 remains conditional; R3 still needs the author's decision.

## Paper implications

Conventional local queue batching is implementation infrastructure, not a
novel algorithmic claim. Credit related local techniques and use the optimized
one-node implementation as the strong-scaling baseline. A possible narrower
paper would need measured scaling on high-diameter graphs, explanation of
ACIC's distributed work/coordination tradeoff, and comparison with distributed
alternatives. Show both time and extra work as node count grows, and separate
fixed-size strong scaling from capacity/weak scaling on larger graphs.

The present measurements do not yet establish that contribution. In particular,
beating an unoptimized ACIC one-node baseline is insufficient, and fitting a
larger graph in distributed memory alone does not establish efficient scaling.
The existing C6 gate is unchanged; a narrower claim would require an explicit
revision and new evidence, not relabeling current failures as success.
