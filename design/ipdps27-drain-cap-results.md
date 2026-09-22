# Concurrency limit, fixed version: results (Anvil, 2026-09-22)

This reports the [pre-recorded experiment](ipdps27-drain-cap.md). All jobs
passed validation:

- the gate, job 20853753: 112 serial-verified solves of `acic_drain` with
  `--process-drain-cap 2 +old-scheduler` on two nodes;
- four performance jobs (20853755/20853760 at 2 nodes, 20853759/20853761 at
  4 nodes): 112 solves each, all digest-checked, layouts verified, no
  runtime warnings.

The gate and performance jobs together used **137 SU**.

**Headline:** a fixed cap on concurrent drainers gives road its first real
multi-node scaling. At 4 nodes, 8 × 15 with cap 7 is **1.7–2.1× faster than
the best fixed layout**, and **1.8–2.2× faster than the best one-node ACIC
layout**. On mesh the same caps are a clear loss, so no single constant
serves both graphs.

## Road

Medians of three repetitions, in seconds per training source, with edge
attempts per reachable edge; allocations A / B.

| Nodes | Arm | 1294456 | 5620086 |
|---:|---|---|---|
| 2 | 8 × 7 (baseline) | 0.488 / 0.473 (3.3) | 0.392 / 0.385 (2.7–2.8) |
| 2 | 8 × 15 | 0.659 / 0.590 (5.9–6.7) | 0.478 / 0.448 (4.5–4.7) |
| 2 | 8 × 15 cap 3 | 0.528 / 0.528 (1.6) | 0.480 / 0.475 (1.5) |
| 2 | 8 × 15 cap 5 | 0.404 / 0.399 (1.7) | 0.367 / 0.355 (1.6) |
| 2 | 8 × 15 cap 7 | **0.356 / 0.359** (1.9) | **0.304 / 0.314** (1.7) |
| 4 | 8 × 7 (baseline) | 0.509 / 0.545 (7.0–7.5) | 0.400 / 0.401 (5.5) |
| 4 | 16 × 7 | 0.647 / 0.765 (16–19) | 0.491 / 0.508 (12–13) |
| 4 | 8 × 15 | 0.660 / 0.667 (13) | 0.546 / 0.562 (11) |
| 4 | 8 × 15 cap 3 | 0.349 / 0.350 (2.0) | 0.308 / 0.311 (1.7) |
| 4 | 8 × 15 cap 5 | 0.291 / 0.286 (2.3) | 0.249 / 0.253 (2.0) |
| 4 | 8 × 15 cap 7 | **0.265 / 0.265** (2.7) | **0.223 / 0.233** (2.3) |

The repeated 8 × 7 controls agree with their primary arms within 0.2–7%.

- **Time prediction: confirmed, in both allocations and on both sources.**
  Cap 7 against 8 × 7 at the same node count is 0.73–0.82 at 2 nodes and
  0.49–0.58 at 4 nodes. The prediction required at most 0.90.
- **Work prediction: exceeded.** The prediction was work within 15% of
  8 × 7. Capped work is instead far below it: 0.57–0.62 of 8 × 7 at 2 nodes
  and 0.36–0.41 at 4 nodes. Cap 7 allows up to 56 drainers per node, the
  same as 8 × 7's 56 workers. So the benefit is not only fewer active
  drainers. With 8 more workers per process relaxing incoming updates,
  distances improve before drainers expand the affected vertices.
- **Work tracks the cap, time does not.** Attempts per edge rise with the
  cap (1.6 → 1.9 at 2 nodes), but time falls. Cap 3 does less work than the
  one-node optimum and still loses time, so it starves the wavefront.
- **Boundary:** cap 7 is the fastest arm and the top of the grid. The best
  cap lies between 7 and 15 (uncapped 8 × 15 is much slower). A cap sweep
  continues this (below).

**Road scaling against the best one-node ACIC layout.** The one-node
reference is 16 × 7 with `+old-scheduler` from the pilot's one-node jobs
(20853639/20853641, same binary family and runtime):

| Source | 1 node, 16 × 7 | 4 nodes, 8 × 15 cap 7 | Speed-up |
|---|---|---|---|
| 1294456 | 0.573 / 0.522 | 0.265 / 0.265 | 1.97–2.16× |
| 5620086 | 0.430 / 0.426 | 0.223 / 0.233 | 1.83–1.93× |

On the fixed layouts, road's best time got *worse* from one to four nodes.

**Against GAPBS (not yet a win).** Anvil's tuned one-node GAPBS solves road
in 0.117–0.142 s on these training sources. Four-node capped ACIC is still
1.9–2.0× slower.

## Mesh

| Nodes | 16 × 7 | 8 × 15 | 8 × 7 | 8 × 15 cap 7 | cap 5 | cap 3 |
|---:|---|---|---|---|---|---|
| 2 | **0.54–0.63** | 0.68–0.76 | 0.80–0.89 | 0.90–1.02 | 1.11–1.28 | 1.64–1.86 |
| 4 | **0.36–0.42** | 0.48–0.59 | 0.50–0.60 | 0.52–0.64 | 0.65–0.71 | 0.92–0.98 |

- **Mesh prediction: disconfirmed.** Every capped arm is slower than 16 × 7,
  by 37–69% at cap 7. Caps cut mesh work by 25–40% (to about 1.4–1.6
  attempts per edge), but mesh's wide frontier needs the parallelism.
- The best setting per graph is opposite: road wants roughly half its
  workers draining, mesh wants all of them. **No fixed constant serves
  both.** This is the case for choosing the cap per graph at run time.

## Consequences

1. **A performance win for road scaling.** On training sources, in two
   allocations, road now scales about 2× from one node to four, where every
   fixed layout lost time. Against GAPBS, road is still behind.
2. **The live controller has a concrete target.** A histogram- or
   rework-driven cap (C3c) must reach the best fixed road cap on road
   while resolving to off on mesh. This is the adaptivity claim: the
   solver decides, from live message flow, how many workers expand work.
   A fixed per-graph table would be tuning, not a mechanism.
3. **RMAT is untested.** `--process-share auto` resolves off on the RMAT
   regression graphs, so the drain cap (which acts in
   `process_shared_heap()`) is inactive there. That must be confirmed in R2,
   not assumed.

## Cap sweep (recorded before submission)

The grid's boundary winner must be resolved before the controller is built,
and eight-node road with a cap has not been measured.

- **Jobs:** road only, two allocations per node count, `acic_drain`,
  `+old-scheduler`, training sources, one warmup and three repetitions
  (`scripts/anvil/layout_check.sbatch` with `GRAPHS=road-usa-z`).
  - 4 nodes: `8x7`, `8x15_cap{7,9,11,13}`, `8x15_cap7_control`
    ([config](../benchmarks/capsweep-4n-variants.json)).
  - 8 nodes: `8x7`, `8x15_cap{5,7,9,11}`, `8x15_cap7_control`
    ([config](../benchmarks/capsweep-8n-variants.json)).
- **Predictions:**
  - At 4 nodes, the best cap is 7 or 9. Caps 11 and 13 are slower than the
    best by more than control noise, and rising toward uncapped 8 × 15.
  - At 8 nodes, the best capped arm is faster than the 4-node cap 7
    (below 0.22 s on both sources), a ≥2.4× speed-up over one-node ACIC.
    It stays slower than one-node GAPBS (0.12–0.14 s): no road win against
    GAPBS is expected from a fixed cap.
  - If the best cap differs between 4 and 8 nodes, it is a further reason
    for a live controller rather than a constant.
- **Budget:** reservation caps 4 nodes × 10 min (85 SU) and 8 nodes × 12 min
  (205 SU) per allocation, 580 SU in total; about 150 SU expected. Spend
  so far is about 650 SU of the 2,000-SU R0–R2 cap, plus the two 8-node
  pilot jobs still queued.

## Evidence

- Audits: `onenode-data/drain-anvil-{20853755,20853760,20853759,20853761}.json`
- Gate log: `logs/slurm/drain-verify-20853753.out`
- Raw logs: `/anvil/scratch/x-rrao/acic/ipdps27-layout/logs/`
