# Anvil GAPBS reference and 8-node pilot

2026-09-22, recorded before submission. The author asked to follow the
proposed sequence and to secure a performance win in at least one category.
This covers steps 1–2; the concurrency limit (step 3) proceeds in parallel.
All ACIC arms use batch 8 from `48ca9c0` on Reconverse `1233130` with
`+old-scheduler`, which removes the registered-scheduler regression
([layout results](ipdps27-layout-check-results.md)).

## Jobs

**1 node, two allocations.** Each runs the ACIC arms `n1_16x7_os`,
`n1_8x7_os`, `n1_8x15_os` and a repeated `n1_16x7_os_control`, on mesh26-z
and road-usa-z, with two training sources, one warmup and three
repetitions. GAPBS then runs in the same allocation (`benchmarks/gap_anvil.py`,
GAPBS `2972aeb`, rebuilt driver sha256 `0e164aa3…`):

- **Selection:** a grid of threads {32, 64, 96, 112, 128} × delta at
  {¼, ½, 1, 2, 4} of the setting selected on Delta (mesh 128/4096, road
  64/32768). The winner is chosen on training sources only.
- **Timing:** the selected and the Delta settings each get one warmup and
  three repetitions, on the two training and four held-out sources. A
  boundary winner is flagged.

**8 nodes, two allocations.** Arms `n8_16x7_os`, `n8_8x7_os` and a repeated
`n8_16x7_os_control`, with the same graphs, sources and repetitions. 8 × 15
is dropped for cost, because it was never best on road and trailed 16 × 7 on
mesh at every node count.

## Predictions

- **Mesh, 8 nodes, 16 × 7:** 0.33–0.45 s on training sources, extrapolated
  from 1.7–1.9× at 4 nodes on the regressing scheduler. That is 2.0–2.8×
  faster than one-node ACIC. It is the likeliest category for a performance
  win: at or below Anvil's tuned one-node GAPBS on training sources.
- **Road, 8 nodes:** not faster than the best one-node ACIC layout. On the
  regressing scheduler it was 0.70–0.81× at 4 nodes. The old scheduler may
  reduce the collapse, but no road win is expected without the concurrency limit.
- **GAPBS on Anvil:** within ±25% of Delta's mesh 0.41 s and road
  0.17–0.20 s. If it is much faster, the mesh prediction is at risk.

A win claimed from this pilot is a training-source result only. C6 still
requires held-out sources, two allocations and the regression suite (R3).

## Budget

- **Reservations:** 1 node × 25 min, 53 SU per allocation; 8 nodes × 15 min,
  256 SU per allocation. That totals 618 SU at the caps; about 300 SU is
  expected.
- **Spend so far:** about 480 SU of the R0–R2 2,000-SU cap had been used.

## Partial result: one-node jobs and 8-node allocation A

**One node (20853639/20853641; 64 solves each, plus GAPBS, all digest-checked).**

- ACIC's best one-node layout is 16 × 7 on both graphs:
  - mesh: 0.94–0.96 / 0.85 s;
  - road: 0.52–0.57 / 0.43 s.
- **GAPBS on Anvil**, tuned on training sources:
  - mesh: 0.33–0.37 s. The selection differs between allocations (128
    threads with Δ 8192, and 64 with Δ 4096), but the top settings are
    within 3%.
  - road: 0.12–0.14 s, with 64 threads and Δ 65536 in both allocations.
- **GAPBS prediction:** holds for mesh (Delta measured 0.41 s). Road is
  faster than predicted, beyond the ±25% band (Delta measured 0.17–0.20 s).
- **Held-out sources:** GAPBS takes 0.34–0.39 s on mesh and 0.12–0.15 s on road.

**8 nodes, allocation A (20853640; 48 solves).** Medians in seconds for
the two training sources, with attempts per edge:

| Graph | 16 × 7 | 16 × 7 control | 8 × 7 | GAPBS, 1 node |
|---|---|---|---|---|
| mesh | 0.467 / 0.338 (5.8 / 4.2) | 0.461 / 0.355 | **0.386 / 0.336** (2.5 / 2.3) | 0.357–0.367 / 0.331–0.332 |
| road | 0.979 / 0.548 (50 / 26) | 0.908 / 0.585 | 0.600 / 0.485 (17 / 13) | 0.14 / 0.12 |

- **Mesh prediction: partly met.** The time lies within the predicted
  0.33–0.45 s, but there is no win over GAPBS. The best ACIC setting, 8 × 7,
  is 1.02–1.08× GAPBS's time: parity, not a win. At 8 nodes, 16 × 7's work
  jumps to 4–6 attempts per edge, so 8 × 7 is best there.
- **Road prediction: confirmed.** Without a limit, 8 nodes are no faster
  than one.
- Allocation B (20853642) is still queued.

## 8-node heap-slice test (recorded before submission)

The [round-cadence check](ipdps27-round-cadence.md) found that
`--heap-slice 8` cuts work and time on both graphs at 2 nodes. At 8 nodes,
mesh's remaining gap to GAPBS is rework, which is the thing slices reduce.

- **Arms** (`acic_slice`, `+old-scheduler`, batch 8, mesh and road,
  training sources, one warmup and three repetitions, two allocations):
  - `n8_8x7` (the pilot's best, as the baseline);
  - `n8_16x7_s8` and `n8_8x7_s8`;
  - `n8_8x15_s8_cap9` (slice and cap combined);
  - a repeated `n8_16x7_s8_control`.

  See the [config](../benchmarks/slice-8n-variants.json).
- **Mesh prediction (the win category):** the best slice-8 arm is at or
  below one-node tuned GAPBS on both training sources in both allocations.
  That is at most 0.357 s on 22442342 and 0.331 s on 41856222; expected
  0.28–0.33 s. If it holds, C6 acceptance on held-out sources follows (R3).
- **Road prediction:** the best slice arm is at least 1.8× faster than
  one-node ACIC (at most 0.24 s on 5620086 and 0.32 s on 1294456). It is
  still slower than GAPBS.
- **Disconfirmation:** if mesh with slice 8 is not faster than 8 × 7 by more
  than control noise at 8 nodes, then the slice's benefit does not survive
  scaling. Mesh then has no route to a GAPBS win at 8 nodes without a new
  mechanism.
- **Budget:** 8 nodes × 12 min reserved per allocation (205 SU each); about
  60 SU each expected. Spend so far is about 770 SU of the 2,000-SU cap,
  with about 170 SU more queued.
