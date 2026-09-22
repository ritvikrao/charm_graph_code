# Why the drain cap works: round cadence or drainer count?

2026-09-22, recorded before submission. This is a mechanism check that must
come before the live controller (C3c), because it decides what the
controller should adjust.

## Observation

The [drain-cap results](ipdps27-drain-cap-results.md) show that the cap cuts
road work far below 8 × 7, even though cap 7 allows the same 56 drainers per
node. The solve logs show a second change that goes with the cap: capped
runs complete many more controller rounds.

| 4 nodes, road, source 5620086 | Compute s | Rounds | ms per round | Distance changes per vertex |
|---|---|---|---|---|
| 8 × 15 | 0.600 | 110 | 5.5 | 12.7 |
| 8 × 7 | 0.400 | 162 | 2.5 | 6.0 |
| 8 × 15 cap 7 | 0.224 | 380 | 0.59 | 2.4 |
| 8 × 15 cap 3 | 0.308 | 996 | 0.31 | 1.9 |

Mesh shows the same thing (16 × 7: 271 rounds; 8 × 15 cap 7: 1,503), but
there more rounds coincide with lost time.

Rounds are closed-loop: each one is a histogram reduction plus a threshold
broadcast. A PE handles the broadcast only between its drain slices, which
are up to 100 shared-queue entries. With fewer drainers, rounds turn over
faster, thresholds track the frontier more closely, and less speculative work
is expanded. That is one candidate mechanism (**cadence**). The other is the
one assumed in the drain-cap plan (**breadth**): fewer concurrent expanders,
with the remaining workers relaxing incoming updates sooner.

## Test

A new readonly flag, `--heap-slice N` (1–100, default 100, the existing
constant), sets how many shared-queue entries one `process_heap()` call
expands before yielding. Shorter slices let every worker reach the
broadcast sooner without limiting how many workers drain.

- Binary `acic_slice` (batch 8, `+old-scheduler`), 32 serial-verified login
  solves at slices 1, 12, 25 (with cap 2) and 100.
- Gate: the two-node 112-solve verification with `--heap-slice 8
  --process-drain-cap 3`.
- Two nodes, road and mesh, two allocations, training sources, one warmup
  and three repetitions. Arms: `8x15`, `8x15_s25`, `8x15_s8`, `8x15_cap7`,
  `16x7`, `16x7_s8` and a repeated `8x15_cap7_control`
  ([config](../benchmarks/slice-2n-variants.json)).

## Predictions and what follows

- **Cadence:** if `8x15_s8` raises road's round count to at least 3× that
  of `8x15`, and brings road work to within 15% of `8x15_cap7`, cadence is
  the mechanism. The live controller should then adjust the slice (or the
  round cadence) rather than the number of drainers, and mesh may also gain
  from `16x7_s8`.
- **Breadth:** if short slices raise the round count but leave road work
  well above cap 7 (more than 1.3×), breadth is the mechanism, and the
  controller adjusts the cap, as planned.
- **Mixed:** anything in between means both matter. The controller then
  adjusts the cap and uses the slice as a fixed setting.
- Prior: breadth. Short slices add scheduler round trips per entry, and the
  capped arms' extra rounds may be a symptom of less work rather than its cause.

## Budget

Gate about 7 SU (reservation 43 SU); two 2-node jobs, about 20 SU each
(reservation 85 SU each).
