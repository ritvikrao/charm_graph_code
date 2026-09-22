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
