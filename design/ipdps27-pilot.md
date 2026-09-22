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
- **Allocation B (20853642; 48 solves) reproduces A.** Its results:
  - mesh: 8 × 7 at 0.379 / 0.331 s, which is 1.00–1.06× GAPBS, and 16 × 7
    at 0.450 / 0.345 s;
  - road: 8 × 7 at 0.562 / 0.483 s, and 16 × 7 at 0.946 / 0.550 s.

  The repeated 16 × 7 controls agree with their primary arms within 1–7% in
  both allocations. The two 8-node pilot jobs together used 86 SU.

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

### Result: 8-node heap-slice test (jobs 20861039/20861040)

- **Validation:** both allocations validate 80 of 80 solves, with no
  runtime warnings.
- **Cost:** 128 SU.

Medians in seconds for the two training sources (attempts per edge):

| Graph | Arm | Allocation A | Allocation B |
|---|---|---|---|
| mesh | 8 × 7 | 0.383 / 0.339 (2.4 / 2.3) | 0.382 / 0.342 |
| mesh | 8 × 7 slice 8 | 0.345 / 0.304 (1.9 / 1.8) | 0.346 / 0.303 |
| mesh | **16 × 7 slice 8** | **0.296 / 0.255** (3.0 / 2.5) | **0.292 / 0.238** |
| mesh | 16 × 7 slice 8, control | 0.296 / 0.239 | 0.294 / 0.244 |
| mesh | 8 × 15 slice 8 cap 9 | 0.472 / 0.425 | 0.482 / 0.433 |
| road | 8 × 7 | 0.581 / 0.453 (16 / 12) | 0.636 / 0.455 |
| road | 8 × 7 slice 8 | 0.282 / 0.243 (6.8 / 5.1) | 0.297 / 0.247 |
| road | 16 × 7 slice 8 | 0.292 / 0.236 (13 / 9.6) | 0.336 / 0.229 |
| road | 16 × 7 slice 8, control | 0.278 / 0.219 | 0.299 / 0.238 |
| road | 8 × 15 slice 8 cap 9 | **0.275** / 0.240 (4.7 / 3.7) | **0.274** / 0.234 |

The repeated controls agree with their primary arms within 0–6% on mesh
and 4–11% on road.

**Mesh: faster than one-node GAPBS on training sources, in both
allocations.** The GAPBS reference is the fastest per-source median across
both one-node allocations and both timed settings: 0.356 s on 22442342 and
0.331 s on 41856222.

- 8-node ACIC with 16 × 7 and slice 8 takes **0.72–0.83 of GAPBS's time**
  (1.20–1.39× faster), including the control arms.
- It is 3.2–3.6× faster than one-node ACIC, and 23–30% faster than the
  pre-slice 8-node best, 8 × 7.
- **Prediction met:** 0.24–0.30 s, at or below GAPBS on both sources in both
  allocations. The disconfirmation did not occur: slice 8 is faster than
  8 × 7 by far more than control noise.
- Adding the cap costs mesh 60% against 16 × 7 slice 8, so slice, not cap,
  is the mesh setting.

**Road: prediction met, still behind GAPBS.**

- The best arms take 0.27–0.28 s on 1294456 and 0.22–0.24 s on 5620086.
  That is 1.9–2.1× faster than one-node ACIC.
- It is still 1.9× slower than GAPBS (0.142 / 0.117 s).
- 8 × 15 with slice 8 and cap 9 is the steadiest road arm, with work at
  3.7–4.7 attempts per edge. It is no faster than the capped arms of the
  8-node cap sweep (0.18–0.25 s).

**Status:** the win is on training sources, which were also used to choose
the setting. The C6 claim needs held-out sources; see
[ipdps27-c6-mesh.md](ipdps27-c6-mesh.md).
