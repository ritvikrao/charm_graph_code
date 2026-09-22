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

## Result (jobs 20857228–30, 2026-09-22)

- **Validation:** the gate passes 112 serial-verified solves with slice 8
  and cap 3. Both allocations validate 112 of 112 solves with no runtime
  warnings.
- **Cost:** 39 SU for the gate and both allocations.

Medians in seconds per training source, with edge attempts per reachable
edge in parentheses. Rounds are the mean over source 0's launches in
allocation A.

| Graph | Arm | Allocation A | Allocation B | Rounds |
|---|---|---|---|---:|
| road | 8 × 15 | 0.576 / 0.420 (5.7 / 4.4) | 0.587 / 0.452 | 132 |
| road | 8 × 15 slice 25 | 0.440 / 0.359 (4.0 / 3.3) | 0.416 / 0.343 | 188 |
| road | 8 × 15 slice 8 | 0.367 / 0.313 (3.1 / 2.7) | 0.378 / 0.308 | 275 |
| road | 8 × 15 cap 7 | 0.351 / 0.307 (1.9 / 1.7) | 0.359 / 0.307 | 1,067 |
| road | 16 × 7 | 0.526 / 0.438 (6.6 / 5.5) | 0.540 / 0.460 | 127 |
| road | **16 × 7 slice 8** | **0.283 / 0.246** (3.1 / 2.8) | **0.292 / 0.247** | 334 |
| mesh | 8 × 15 | 0.786 / 0.694 (2.0 / 1.8) | 0.785 / 0.680 | 199 |
| mesh | 8 × 15 slice 8 | 0.704 / 0.589 (1.7 / 1.5) | 0.720 / 0.612 | 559 |
| mesh | 8 × 15 cap 7 | 1.046 / 0.910 (1.6 / 1.4) | 0.998 / 0.892 | 3,448 |
| mesh | 16 × 7 | 0.635 / 0.551 (2.1 / 1.9) | 0.626 / 0.541 | 223 |
| mesh | **16 × 7 slice 8** | **0.550 / 0.495** (1.7 / 1.6) | **0.550 / 0.498** | 586 |

The repeated cap 7 controls agree with their primary arms within 0–4%.

**Against the recorded rule: breadth, for work.** Slice 8 raises road's
round count only 2.1× (the cadence branch required 3×). It leaves road work
at 1.6× cap 7's (the breadth branch starts at 1.3×). So the cap's much
lower rework is not explained by round cadence alone.

**But the time result is what matters, and it is new:**

- **Short slices help both graphs.** At 8 × 15, slice 8 is within 0–5% of
  cap 7's time on road. On mesh it is 8–15% *faster* than uncapped, where
  cap 7 is 27–33% slower.
- **16 × 7 with slice 8 is the best setting measured, on both graphs.** On
  road it is 19–20% faster than cap 7 and 44–46% faster than 16 × 7. On mesh it
  is 8–13% faster than 16 × 7, the previous best.
- **It is one constant that does not hurt mesh**, which the cap failed to
  achieve. The prior (breadth, slices only add overhead) was wrong about
  time.
- **Two-node road at 0.25–0.29 s** is 1.7–2.0× faster than one-node ACIC
  (0.43–0.57 s).

**Consequences:**

- Slice length is now the primary fixed setting, and 16 × 7 slice 8 is the
  new baseline at 2 nodes. The cap stays a candidate on top of it; a
  combination is tested at 8 nodes.
- The live controller's actuator is still open. Both knobs work through
  how quickly workers return to the scheduler and how many expand at once.
  A controller must beat 16 × 7 slice 8, not 8 × 7.
- One-node and RMAT behaviour of short slices is unmeasured. A slice of 8
  adds a scheduler round trip per queue batch. On RMAT graphs process
  sharing resolves off, so `process_shared_heap()`, and with it the slice,
  is inactive there.

Audits: `onenode-data/slice-anvil-{20857229,20857230}.json`.
