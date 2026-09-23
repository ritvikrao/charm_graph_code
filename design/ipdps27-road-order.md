# Road: the controller never orders road's work

2026-09-22, recorded before submission. The author asked to prioritise road
over mesh, because road is real data. Road at 8 nodes is at best 0.18–0.25 s,
against 0.12–0.14 s for tuned one-node GAPBS.

## Diagnosis

**Road runs with no global ordering.**

- The histogram has 2,048 absolute buckets. At the default `logv` width
  (17.0 on road-usa-z), it covers 35K distance units. Road distances reach
  37–54M.
- Every solve log reports the frontier outside the reduced window in all but
  2–3 rounds. A login-node round trace (one process, 6 workers, source
  5620086) shows `first_nonzero` in the overflow slot in 604 of 643 rounds.
  The heap threshold then admits everything.
- So on road the only ordering is each process's own shared priority queue.

**Throughput is not the limit.**

- 8-node capped ACIC makes 2.7–4.7 edge attempts per edge. GAPBS made 1.44–1.45
  on Delta.
- It does 2–3× GAPBS's work, in 1.5–2× the time, on about 15× the cores. Per
  core it runs at about a tenth of GAPBS's rate.
- The cost is speculative work: processes expanding their own blocks far
  ahead of the global frontier. That is the attribution result (work grows
  with global concurrency), and the reason the drain cap helps.

**What changing the width does at one process:** nothing. `--range-extend`
overshoots, to scale 32768 (width 557K), and is 2.7× slower. With the weight
rule, or live slack at widths 8192, 32768 or 368855, time is 10.9–11.2 s and
work is 0.587 wasted updates per edge in every arm, with the same digest.
With one process, the process queue already orders everything, so a global
window can only matter across processes.

## Hypothesis and test

A Δ-stepping-like global admission window restores cross-process ordering:

- the histogram is in range (`--bucket-width 32768` covers 67M);
- live slack (`--slack-control on`) sets the heap threshold at the global
  frontier plus a live-adjusted number of widths (1–256, starting at 8).
  LiveSlack tightens when distance changes per retired update rise, and
  loosens when more than half the PEs are idle.

This is the author's adaptivity: admission set every round from live flow.
Live slack was last evaluated before process sharing, batching and nearest
priority, and never in range on road-usa.

**Arms** (8 nodes, road only, training sources, one warmup and three
repetitions, two allocations, `+old-scheduler`, batch 8; see the
[config](../benchmarks/road-order-8n-variants.json)):

| Arm | Layout | Settings |
|---|---|---|
| `16x7_s8` | 16 × 7 | slice 8 (baseline) |
| `16x7_s8_w32k` | 16 × 7 | slice 8, width 32768, slack off |
| `16x7_s8_w32k_slack` | 16 × 7 | slice 8, width 32768, slack on |
| `16x7_s8_weight_slack` | 16 × 7 | slice 8, weight width rule (368855), slack on |
| `8x15_cap7` | 8 × 15 | cap 7 (the best road arm so far) |
| `8x15_cap7_w32k_slack` | 8 × 15 | cap 7, width 32768, slack on |
| `16x7_s8_w32k_slack_control` | 16 × 7 | repeat |

## Predictions

- **Width alone does nothing:** `w32k` with slack off matches the baseline
  within noise. The 0.999 heap percentile admits everything in range too.
- **The window restores ordering:** `w32k_slack` cuts road work at 16 × 7
  slice 8 from about 13 to at most 4 attempts per edge. Time falls by at
  least 20% against the baseline, in both allocations and on both sources.
- **Against the cap:** the best window arm is at least 10% faster than
  `8x15_cap7`.
- **The weight rule is too coarse:** its smallest window is 5.6× GAPBS's
  Δ, so it falls between the baseline and `w32k_slack`.
- **Disconfirmation:** if `w32k_slack` does not beat the baseline by more
  than control noise, cross-process ordering is not road's limit. The next
  candidate is then message latency on the critical path (htram hold and
  flush).

Width 32768 is a road-specific setting for this mechanism test. If it
works, the width is to come from the graph as read, or live from the
observed distance range, and it must not hurt mesh or RMAT.

## Gate and budget

- **Gate:** the two-node 112-solve verification with `--slack-control on
  --bucket-width 32768 --heap-slice 8 --process-drain-cap 3`.
- **Jobs:** two 8-node allocations × 12 min, 205 SU each at the cap; about
  60 SU each expected, plus the gate.
- **Spend so far:** about 945 SU of the 2,000-SU cap.

## Result (jobs 20868020–22, 2026-09-23)

The gate passes: 112 of 112 serial-verified solves. Both 8-node
allocations validate 56 of 56 road solves, digest-checked, with no runtime
warnings. About 108 SU in total.

The first audit run failed on the checker, not the solves: it required
`Live slack: off` in every launch. It now requires the value of the arm's
last `--slack-control` flag.

Medians of three repetitions, in seconds, for sources 5620086 / 1294456,
allocation A (20868021) then B (20868022). The last column gives edge
attempts per reachable edge.

| Arm | A | B | Attempts per edge |
|---|---|---|---|
| `16x7_s8` (baseline) | 0.235 / 0.330 | 0.220 / 0.283 | 9.0–9.7 / 12.5–14.9 |
| `16x7_s8_w32k` | 0.268 / 0.425 | 0.292 / 0.460 | **1.5** / **1.5** |
| `16x7_s8_w32k_slack` | 0.196 / 0.236 | 0.176 / 0.231 | 5.8–7.4 / 8.5 |
| `16x7_s8_w32k_slack_control` | 0.174 / 0.232 | 0.179 / 0.231 | 5.8–5.9 / 8.4–8.5 |
| `16x7_s8_weight_slack` | 0.210 / 0.251 | 0.212 / 0.249 | 8.8–8.9 / 10.0–10.5 |
| `8x15_cap7` | 0.181 / **0.215** | **0.177** / **0.214** | 3.4 / 4.2–4.3 |
| `8x15_cap7_w32k_slack` | 0.192 / 0.257 | 0.187 / 0.222 | 3.0–3.1 / 3.5–3.6 |

In cell A, 5620086, the repeated slack arm disagrees with its primary by
11% (0.174 against 0.196 s). The other three cells agree within 2%.

**Against the predictions:**

- *Width alone does nothing:* **disconfirmed, and the most informative
  result.** With the histogram in range, the 0.999 percentile does order
  road. Work falls from 9–15 to 1.5 attempts per edge, about GAPBS's 1.44
  on Delta. But time rises 1.14–1.63× over the baseline.
- *The window restores ordering, at most 4 attempts per edge and at least
  20% faster:* **partly.** Time falls 17–30% against the baseline, and by
  at least 20% in three of four cells (A, 5620086 is 17%, or 26% by its
  control). Work is 5.8–8.5 attempts per edge, not at most 4.
- *The best window arm is at least 10% faster than cap 7:* **disconfirmed.**
  Cap 7 is the best road arm or tied: 0.96–1.08× the best window arm on
  5620086, and 7–10% faster on 1294456. Adding the window to cap 7 cuts
  work 8–17% and costs 3–20% in time.
- *The weight rule lies in between:* confirmed on both sources, in both
  allocations.
- *Disconfirmation clause:* not triggered. The window beats the baseline by
  more than noise, so cross-process ordering matters. What limits the
  ordered solve is the controller round, which the clause did not foresee.

**Why the fully ordered arm is slow: it is bound by rounds.** In `w32k` the
heap threshold ends at bucket 1122 (5620086) and 1658 (1294456) after
889–911 and 1436–1444 rounds. It advances 1.15–1.23 buckets (38–40K
distance) per round, and the median round takes 0.21–0.25 ms. Rounds are
back to back, so the solve is about 0.85 D/W rounds of about 0.23 ms. That
is 0.20–0.35 s on these sources, whatever the work. The earliest rounds,
with a few thousand updates in flight, already take 0.18–0.27 ms. So most
of a round is the reduction and broadcast over 896 PEs, not work backlog.

Across the uncapped 16 × 7 arms, time ≈ rounds × 0.26 ms + attempts ×
0.29 ns fits the baseline, `w32k` and `w32k_slack` cells in allocation A
within 6% (two of the six were used to fit it). With a
closed loop, rounds ≈ time / round length, so this says a round costs
0.26 ms plus its share of the work. It is not an independent cause.

**Where road stands.** The best road arm at 8 nodes is 0.174–0.196 s on
5620086 and 0.214–0.215 s on 1294456. Tuned one-node GAPBS is 0.117–0.119 s
and 0.142 s. ACIC is still 1.5× slower, on both sources.

**Consequence.** The ordered regime needs 1.5 attempts per edge, so work
costs about 25 ms of the solve and rounds cost the rest. Two levers
remain:

- **fewer rounds:** a wider bucket, so the threshold moves more distance
  per round;
- **cheaper rounds:** a control path whose round costs less than
  0.2 ms.

Unordered and capped arms do 3–15 attempts per edge. A fully ordered arm at
GAPBS-like work needs a round of at most about 0.1 ms, or at most about 500
rounds on 1294456, to reach 0.14 s.

## Next screen: fewer rounds or cheaper rounds (recorded before submission)

This tests both levers on the ordered regime, road only, at 8 nodes, with
slack off. It uses the same protocol as above: training sources, one warmup
and three repetitions, two allocations. See the
[config](../benchmarks/road-rounds-8n-variants.json).

| Arm | Settings |
|---|---|
| `16x7_s8_w32k` | anchor, as above |
| `16x7_s8_w32k_node` | plus `--control node --control-interval 0.05` |
| `16x7_s8_w128k` | width 131072 |
| `16x7_s8_w128k_node` | width 131072, node control |
| `16x7_s8_w512k` | width 524288 |
| `8x15_cap7` | anchor, the best road arm |
| `16x7_s8_w128k_control` | repeat |

`--control node` sums contributions per process in shared memory and
returns the thresholds through shared state. It was slower on RMAT in 8c:
under load, a round still waited for the slowest PE to reach a pickup
point. Road's ordered rounds carry little work, and slice 8 brings PEs back
to a pickup point often. The 0.05 ms interval sits below the 0.21–0.25 ms
reduction round; the default 0.25 ms would be a floor above it. On the
login node, at one process, width 32768 with node control gives the same
digest as the reduction path.

**Predictions:**

- **Wider buckets cut rounds.** `w128k` needs at most 0.45× the rounds of
  `w32k`, at most 3 attempts per edge. It is at least 25% faster than
  `w32k`, and faster than `8x15_cap7` on both sources in both allocations.
- **The curve turns.** `w512k` makes at least 1.5× `w128k`'s attempts per
  edge and is not faster than `w128k` by more than control noise.
- **Node control shortens rounds.** In `w32k_node` the median round is at
  most 0.15 ms (against 0.21–0.25 ms), and the arm is at least 25% faster
  than `w32k`. `w128k_node` is faster than `w128k`. If the median round
  does not shorten, round cost is set by PEs reaching the pickup point, not
  by the reduction path. The next step would then be a round that does not
  wait for every PE.
- **Against GAPBS (0.117–0.119 s on 5620086, 0.142 s on 1294456):** not
  expected. The best arm is predicted at 0.12–0.17 s. A win on both
  sources in both allocations would be the first road win.

Widths 131072 and 524288 are again settings for testing the mechanism. The
width must end up derived from the graph as read, or from live round
statistics: for example, widen while the threshold advances about one
bucket per round and work stays near its floor. It must then be checked on
mesh and RMAT.

**Gate:** the two-node 112-solve verification with `--control node
--control-interval 0.05 --bucket-width 131072 --heap-slice 8
--process-drain-cap 3 +old-scheduler`.

**Budget:** two 8-node allocations × 12 min. Expect about 55 SU each (the
previous screen ran 3 min), plus a 4-SU gate. Spend so far is about
1,055 SU of the 2,000-SU cap.

### Rounds screen result (jobs 20876828–30, 2026-09-23)

The gate passes: 112 of 112 serial-verified solves with node control and
width 131072. Both 8-node allocations validate 56 of 56 road solves, with
no runtime warnings. About 105 SU in total. Spend so far is about 1,160 SU
of the 2,000-SU cap.

**These allocations are slower.** The `8x15_cap7` anchor ran 0.231–0.234 /
0.243–0.249 s, against 0.177–0.181 / 0.214–0.215 s in 20868021/22. That is
13–32% slower. `w32k` is 0–25% slower. So only comparisons within an
allocation hold.

Medians in seconds for 5620086 / 1294456 (A = 20876829, B = 20876830), with
attempts per edge and rounds (1294456's rounds in brackets):

| Arm | A | B | Attempts per edge | Rounds |
|---|---|---|---|---|
| `w32k` | 0.335 / 0.471 | 0.334 / 0.459 | 1.45–1.49 | 887–893 (1439–1444) |
| `w32k_node` | 0.387 / 0.476 | 0.311 / 0.442 | 1.49–1.54 | 878–909 (1435–1443) |
| `w128k` | 0.234 / 0.252 | 0.239 / 0.248 | 1.73–1.97 | 519–525 (784–799) |
| `w128k_control` | 0.246 / 0.258 | 0.249 / 0.353 | 1.75–1.95 | 524–526 (793) |
| `w128k_node` | **0.197** / 0.259 | **0.227** / 0.271 | 2.2–3.2 / 2.2–2.3 | 440–502 (791–798) |
| `w512k` | 0.238 / 0.296 | 0.238 / 0.300 | 8.4–8.5 / 11.1–11.2 | 247–264 (295–345) |
| `8x15_cap7` | 0.234 / **0.249** | 0.231 / **0.243** | 3.9–4.0 / 4.6–4.7 | 176–222 (261–300) |

B's `w128k_control` on 1294456 spreads 0.246–0.379 s over its repetitions.
Every other w128k cell agrees within 5%.

**Against the predictions:**

- *`w128k` needs at most 0.45× `w32k`'s rounds:* **disconfirmed.** It
  needs 0.54–0.59×. Work stays at most 3 attempts per edge (1.7–2.0):
  confirmed. It is at least 25% faster than `w32k`: confirmed, 28–30% on
  5620086 and 46% on 1294456.
- *`w128k` beats cap 7 on both sources in both allocations:*
  **disconfirmed, a tie.** It is within 0–3% of cap 7 in every cell, while
  doing 40–60% less work (1.7–2.0 against 3.9–4.7 attempts per edge).
- *The curve turns by `w512k`:* confirmed. Work rises 4.3–6.1× to 8.4–11.2
  attempts per edge. The threshold runs 9–11 buckets ahead of the frontier,
  so the arm is effectively unordered. It is no faster on 5620086 and
  17–21% slower on 1294456.
- *Node control shortens rounds to at most 0.15 ms:* **disconfirmed.**
  `w32k_node`'s median round is 0.24–0.30 ms, the same as the reduction
  path, and the arm is 16% slower to 7% faster. `w128k_node` is 5–16% faster
  on 5620086 and 5–9% slower on 1294456. It gains on 5620086 by doing more
  work in fewer rounds (2.2–3.2 attempts per edge), so it orders less
  rather than rounding faster.
- *No GAPBS win:* confirmed. The best cell is 0.197 s against 0.117–0.119 s.

**How the frontier moves.** Per round, the frontier bucket advances
38–42K distance at width 32K, 68–83K at 128K and 133–176K at 512K. It does
not move at all in 32–37%, 55–59% and 74–80% of rounds. A 4× wider bucket
moves the frontier only about 1.7× as far per round: the lowest bucket needs
2–3 rounds to settle. Settling it needs relaxations to chain across
processes, so per-hop message latency also bounds the frontier.

**Where a round's time goes.** Take the median length of the early rounds,
those before 20,000 updates have been processed, across every road log on
disk (jobs from 1 to 8 nodes):

| Layout, nodes | Total PEs | Processes | Early round |
|---|---:|---:|---:|
| 8 × 7, 1 | 56 | 8 | 0.036 ms |
| 16 × 7, 1 / 8 × 7, 2 | 112 | 16 | 0.045–0.048 ms |
| 8 × 15, 1 | 120 | 8 | 0.057 ms |
| 8 × 7, 4 / 16 × 7, 2 | 224 | 32 | 0.071–0.080 ms |
| 8 × 7, 8 / 16 × 7, 4 | 448 | 64 | 0.115–0.125 ms |
| 8 × 15, 4 | 480 | 32 | 0.109–0.138 ms |
| 8 × 15, 8 | 960 | 64 | 0.144–0.194 ms |
| 16 × 7, 8 | 896 | 128 | 0.215–0.244 ms |

An idle round grows about linearly with the PE count, roughly 0.2 µs per PE
at 8 nodes, not logarithmically. Every Reconverse build on Anvil has
`SPANTREE 0` in its generated `converse_config.h`, carried over in the
CMake cache. Without it, `CmiSyncBroadcast`/`CmiSyncBroadcastAll` loop
over every PE from the sender. So each round's threshold broadcast is 895
back-to-back sends from PE 0 at 16 × 7 on 8 nodes. Charm++'s reduction
tree comes from the `CmiSpanTreeParent` macros, which do not depend on
`SPANTREE`.

One observation cuts against this: node control broadcasts to 128
processes, not 896 PEs, and its rounds were no shorter. So the flat
broadcast is the hypothesis to test, not a finding.

**Consequence.** `w128k` matches the best road arm at half its work, and
spends most of its time in 520–800 rounds of 0.28–0.37 ms. Road's limit is
now the controller round, and that round costs a microsecond per few PEs.
A cheaper round converts directly into road time: at 0.1 ms per round,
`w128k` would be about 0.10–0.14 s.

## Next screen: spanning-tree broadcast (recorded before submission)

**Build.** Reconverse at the same commit (`1233130`, which is itself the
upstream commit "spantree on by default"), configured with `SPANTREE=ON`,
in `~/charm_reconverse/reconverse-linux-x86_64-mpicxx-v0923-span`. Its CMake
cache differs from v0921-shm only in `SPANTREE` and the pinned tag. Building
it re-pointed the root `bin`/`lib`/`include`/`tmp` links, and they were
restored to v0921-shm.

`acic_span` is built from the same sources as `acic_slice` (byte-identical
application sources, htram `7db9c0a`) and links the v0923-span runtime
through its own RPATH. `acic_slice` still resolves to v0921-shm.

On the login node, road with width 131072 gives the reference digest at one
process (6 PEs) and at four (lcrun, 2 PEs each).

**Arms** (8 nodes, road, same protocol; see the
[config](../benchmarks/road-span-8n-variants.json)): `w32k`, `w128k` and
`8x15_cap7`, each on `acic_slice` (flat broadcast) and on `acic_span`
(tree), plus `w64k_span` and a repeat of `w128k_span`.

**Predictions:**

- **Tree broadcast cuts the idle round.** At 16 × 7 the early-round median
  falls from 0.215–0.244 ms to at most 0.12 ms. The ordered arms' median
  round falls by at least 0.1 ms.
- **The ordered arms gain most.** `w32k_span` is at least 30% faster than
  `w32k`. `w128k_span` is at least 20% faster than `w128k`, and at least
  10% faster than flat `8x15_cap7` on both sources in both allocations.
  Work changes by less than 10%.
- **The cap gains less.** It runs 176–300 rounds against 520–1440, so
  `8x15_cap7_span` is within 10% of `8x15_cap7`.
- **With cheaper rounds, the best width narrows or holds:** `w64k_span` is
  within 10% of `w128k_span`.
- **Against GAPBS:** `w128k_span` reaches 0.13–0.18 s on 5620086, still
  not below GAPBS's 0.117–0.119 s.
- **Disconfirmation:** node control already cut the broadcast to 128 sends
  without shortening rounds, so this may fail. If the early round does not
  shorten, the per-round cost lies in the reduction or in Main's
  per-round work on PE 0. That is measured next, with timers around
  `reduce_histogram` and the broadcast.

Tree broadcast changes every Charm++ broadcast, so before it is adopted
the C6 mesh and R2 RMAT checks must be repeated on it.

**Gate:** the two-node 112-solve verification on `acic_span` with
`--bucket-width 131072 --heap-slice 8 --process-drain-cap 3
+old-scheduler`.

**Budget:** two 8-node allocations × 12 min, about 60 SU each, plus a 4-SU
gate. Spend so far is about 1,160 SU.
