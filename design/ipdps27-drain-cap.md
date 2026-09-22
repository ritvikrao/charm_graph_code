# Concurrency limit, fixed version: `--process-drain-cap`

2026-09-22, recorded before submission. This is step 3 of the author-approved
sequence. The [attribution](ipdps27-r1-attribution-results.md) found that
road's rework grows with the number of workers expanding shared work at once.

## Mechanism

`--process-drain-cap N` (0 = off, the default) allows at most N workers of
each process inside `process_shared_heap()` at the same time.

- A per-process atomic counter on the `ControlNode` group holds the slots.
  It is acquired by CAS and released by a scope guard.
- A worker that finds the cap reached returns without taking work. It keeps
  receiving and relaxing updates, which feed its own bin.
- The idle loop calls `process_heap()` again, so it retries.
- Active drainers steal from every bin, so no work is stranded.

Unlike a smaller layout, the capped workers still spend their cores
absorbing message delivery and the CAS relaxations of incoming updates.

The flag is readonly in `sssp_smp.ci`, so every process sees it.

Checks run so far: 32 serial-verified solves on the login node (sparse and
dense graphs, caps 0–3 with 6 workers). A two-node 112-solve gate
(`VERIFY_BINARIES=acic_drain`, `VERIFY_EXTRA="--process-drain-cap 2
+old-scheduler"`) precedes the performance jobs.

## Experiment

Road and mesh, 2 and 4 nodes, two allocations each, with training sources
and three repetitions after a warmup. All arms use batch 8 with
`+old-scheduler`:

| Arm | Layout per node | Cap |
|---|---|---|
| `8x7` | 8 × 7 | off (the fixed-layout baseline) |
| `16x7` | 16 × 7 | off (the mesh baseline) |
| `8x15` | 8 × 15 | off |
| `8x15_cap{3,5,7}` | 8 × 15 | 3, 5, 7 |
| `8x7_control` | 8 × 7 | off (repeat) |

## Predictions

- **Road:** some cap in {3, 5, 7} brings 8 × 15 work to within 15% of 8 × 7.
  Its time is at least 10% below 8 × 7 at the same node count, in both
  allocations and on both training sources. The extra capped workers absorb
  the receive cost.
- **Disconfirmation:** no cap beats 8 × 7 by more than control noise. The
  fixed cap is then only a layout choice, and a live version of it would
  start from no demonstrated benefit.
- **Mesh:** the best capped arm is not slower than 16 × 7 by more than noise.
- A cap chosen here is one constant across graphs and node counts, not per
  graph. If a live, histogram-driven cap follows, it must beat this constant
  (C3c).

## Budget

- **Reservations:** 2 nodes × 20 min, 85 SU; 4 nodes × 20 min, 171 SU per
  allocation; 512 SU at the caps for all four, plus a 43-SU gate.
- **Expected use:** about 250 SU.
