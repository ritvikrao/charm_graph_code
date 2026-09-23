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
