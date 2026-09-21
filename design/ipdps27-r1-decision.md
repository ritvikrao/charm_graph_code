# R1 decision: test process-wide queue priority

2026-09-21, before implementation. **Proceed with one bounded R1 intervention:
prefer smaller-distance work across a process's shared producer queues.**
Keep sharing, reader tiles and the current slack-off controller. This selects
the excess-relaxations row of the plan. R2 remains conditional on a promising
R1 result; R3 still requires the author's decision.

## Completed R0 evidence

All four full-node jobs completed with exit 0. The two eight-node allocations
were 22237672 (220 s) and 22237673 (202 s). They are separate allocations,
but share seven of eight physical hosts, so they do not establish robustness
across two disjoint node sets. The four jobs used **142.684 allocated CPU-hours**.
Raw-log revalidation passes all **288 solves** (240 ACIC, 48 GAPBS, warmups
included), all 120 diagnostic queue identities, all ACIC direct/production
ledger equalities and offered-update topology sums.

Source medians and ratios follow the [one-node report](ipdps27-r0-one-node-results.md):
two training sources, two timed repetitions, warmups excluded, allocations
kept separate. An edge below is a stored directed adjacency entry. ACIC work
is recovered from the production ledger; GAPBS work is diagnostic.

| Graph | One-node attempts / edge, A / B | Eight-node attempts / edge, A / B | GAPBS attempts / edge, A / B | Paired eight/one work growth, A / B |
|---|---:|---:|---:|---:|
| mesh26-z | 8.511 / 8.657 | 22.049 / 22.424 | 2.613 / 2.613 | 2.577 / 2.578 |
| road-usa-z | 11.581 / 11.247 | 52.444 / 51.700 | 1.446 / 1.444 | 4.527 / 4.560 |

Eight-node candidate times are mesh **0.960 / 0.953 s** and road
**0.715 / 0.696 s**. Paired eight-node ACIC / one-node GAPBS timing ratios
are mesh **2.458 / 1.852** and road **3.594 / 2.868**. The mesh B ratio is
depressed by the retained GAPBS outlier documented in the one-node report.
These are training diagnostics, not final C6 acceptance.

The extra work is mostly repeated expansion, not stale entries accidentally
scanning edges. Diagnostic expansions per reachable vertex are mesh
**21.60 / 21.53** and road **50.76 / 50.92**; stale queue-pop fractions are
about **23.7%** and **7.2%**, respectively. Stale pops already skip expansion.
Measured solver work costs mesh **140 / 139 ns per attempt**, versus
**139 / 146** on one node, and road **181 / 183**, versus **191 / 203**.
The several-fold growth is in operation count, not a comparable increase
in measured solver cost per attempt.

Estimated queue time remains substantial: **44.7–44.9%** of solver work
on mesh and **51.7–51.8%** on road. Work occupies **82.7–82.9%** of summed
solve PE-seconds on mesh and **73.7–75.1%** on road. These overlapping timers
are not critical-path fractions. Pop try-lock failure rates rise to about
0.50% / 1.3%; low retry counts do not rule out blocking push-lock or cache
costs. Offered inter-node updates are about 0.28% / 0.38% of attempts;
these are not network-byte measurements.

Tiling exposes a real work/participation tradeoff at eight nodes. Relative
to sharing alone, it increases mesh attempts **2.68–2.71×** and road
attempts **2.21–2.25×**. It still improves mesh time by **43–45%**, while
road time is 2–4% higher, within observed control variation. Sharing alone
has diagnostic idle-window shares around **77%** on mesh and **60–61%**
on road, versus tiled **9–10%** and **6–8%**. Idle-window measurements are
not hardware CPU-idle measurements. Returning to sharing alone therefore
does not supply a general solution: preserve tiles while testing ordering.

The repeated production road control in eight-node A is 17.0% slower on
source-paired aggregate, but only 3.0% higher in attempts. Source 5620086's
control repetitions are 1.025868 / 0.592476 s with nearly equal work.
No outliers are removed. Other eight-node control timing ratios are
1.025 / 1.016 on mesh and 1.009 on road B. Candidate diagnostic/production
ratios are mesh 1.068 / 1.056 and road 1.026 / 1.093; diagnostic work changes
by roughly -6% to +2.4% at the source-median level. Measure the intervention's
primary work and time outcomes in production builds.

## Mechanism, prediction and disconfirmation

The current pop tries the worker's own producer queue before peer queues.
Each queue is internally ordered, but a worker can choose farther-distance
work while a peer queue holds nearer work. R0 establishes the headroom for
an ordering intervention; it does **not** prove this particular local policy
causes the excess, or that process-local ordering controls global rework.

Add an opt-in `--process-queue local|nearest` policy. `local` preserves the
existing selection. `nearest` publishes a cheap atomic hint for each queue's
head distance and first attempts the smallest advertised head across the
process. Recheck the actual head and existing admission predicate under its
queue lock. Fall back to the existing scan on contention or ineligible work
so a stale hint cannot strand admitted work. Hints are approximate: there is
no global minimum, new barrier, new settling claim or new retirement rule.
Preserve original bucket ordering inside each queue, overflow accounting,
distance CAS, source-reset behavior and the bounded worker yield.

The prediction is fewer intermediate improvements, expansions and scanned
edges, with enough retained participation to reduce time on both node counts.
A useful scale for the target is restoring eight-node work toward the
one-node level: about **9 attempts/edge on mesh and 12 on road**. If all
elapsed cost scaled with that reduction, the observed 2.58× / 4.5× work
growth could cover the present eight-node time gap. This is a motivating
counterfactual, **not a speedup forecast**: fixed costs, lower participation,
extra queue synchronization and delayed messages may prevent it.

Disconfirm the selected intervention if work does not fall reproducibly,
or if reduced work is offset by queue/coordination cost or lost participation.
A small timing-only win without the predicted work reduction does not
validate the ordering explanation. A modest work/time win still does not
justify R3 unless the remaining gap has a credible route to the unchanged
acceptance target. If this one intervention fails, stop and report the
result; do not launch another optimization search or perform R2 by default.

## Bounded R1 comparison

First verify local/nearest, sharing on/off, tiling on/off, repeated sources,
isolated vertices, and concurrent queue draining. Then run the same two
training graphs and sources at one and eight nodes, three timed repetitions
plus warmups, two independent allocations per role. Compare same-binary
local, nearest and repeated local control; include the frozen R0 production
binary and a nearest diagnostic arm. This is a fixed-policy comparison,
not evidence of live adaptation. No graph-name tuning is introduced.

Retain correct digests, the production work ledger, queue accounting and
all outliers. Use at most the existing 12-minute one-node / 6-minute
eight-node reservation limits (256 SU combined), within the 2,000-SU R0–R2
cap. The pilot does not replace held-out, regression or final C6 checks.

Eight-node raw results, manifests, summaries and log hashes are in
[r0-eight-node-22237672.json](onenode-data/r0-eight-node-22237672.json).
Reproduce with `benchmarks/summarize_r0.py CAMPAIGN 22237672 22237673 --output FILE`;
add 22237670/22237671 to include the one-node data in the same output.
