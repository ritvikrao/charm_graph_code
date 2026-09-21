# R1 next: attribution results (Anvil, 2026-09-21)

This reports the [pre-recorded fixed-total-layout experiment](ipdps27-r1-attribution.md).
All three jobs completed; every solve validates. **Road's distributed work
growth is caused by the number of concurrently active workers, not by physical
placement and not by the number of priority domains.** On road, adding workers
beyond about 112 in total makes the solve slower.

## Validation and cost

| Job | Role | Nodes | Elapsed | Core-hours |
|---|---|---|---:|---:|
| 20833717 | Two-node correctness gate | a[871-872] | 47 s | 3.34 |
| 20833718 | Allocation A | a[857-858] | 293 s | 20.84 |
| 20833719 | Allocation B | a[863-864] | 303 s | 21.55 |

The total is **45.7 SU**, against a 213-SU reservation cap.

- **Gate:** 224 solves verified against serial Dijkstra, with 90 connected
  diagnostic solves that carry inter-node work.
- **Performance allocations:** 144 solves each (64 diagnostic, 72 launches),
  all digest-checked. The audit also checks:
  - the resolved process/PE layout of every arm;
  - effective settings and source order;
  - edge accounting and topology sums in diagnostic solves;
  - that no runtime errors, stalls or rescues occurred.
- **No slow repetitions:** none exceeds twice its source median; the largest is 1.18×.

Binaries were rebuilt on Anvil from `48ca9c0`/htram `7db9c0a`:
`acic_r1_batch` sha256 `bd07ed61…`, `acic_r1_batch_diag` `6daa7dba…`. The
hashes differ from Delta's because this is a different build host.

## Attribution chain

These are ratios of production edge attempts per reachable edge (time in
parentheses), each per source, for allocations A / B. D, P and W multiply to T.

| Factor | Road 1294456 | Road 5620086 | Mesh 22442342 | Mesh 41856222 |
|---|---|---|---|---|
| D, 8→16 domains (1 node) | 1.081 / 1.083 (0.92 / 0.88) | 1.019 / 0.981 (0.85 / 0.81) | 1.010 / 1.015 | 1.062 / 1.013 |
| P, 1→2 nodes only | 1.084 / 0.981 (0.96 / 0.90) | 0.967 / 1.003 (0.88 / 0.91) | 0.991 / 0.989 | 1.007 / 1.005 |
| W, 7→15 workers/process (2 nodes) | **1.608 / 1.844** (1.12 / 1.28) | **1.677 / 1.591** (1.20 / 1.11) | **1.226 / 1.202** (0.86 / 0.84) | **1.219 / 1.219** (0.84 / 0.85) |
| T, Delta's 1n→2n step | 1.885 / 1.960 (0.99 / 1.00) | 1.653 / 1.566 (0.89 / 0.82) | 1.228 / 1.207 (0.70 / 0.62) | 1.303 / 1.241 (0.72 / 0.64) |

The noise thresholds max(3f, 5%) are:

| Graph | Allocation A | Allocation B |
|---|---|---|
| Road | 16.3% | 13.8% |
| Mesh | 31.6% | 11.6% |

Mesh A's control arm had one slower repetition set (time ratio 1.08, f = 0.092).

**Outcome against the rule.** Only W is attributed. It exceeds the threshold
on both road sources in both allocations, and on mesh in B. It accounts for
between 75% and all of log T on road, and on mesh.

- D and P are within noise on every road source.
- D is consistently +8% on road source 1294456 in both allocations, but that
  is below the threshold on its own.

Anvil reproduces Delta's scaling step:
- Road work grows 1.57–1.96× here, against 1.65–1.72× on Delta.
- Mesh work grows 1.21–1.30×, against 1.22×.

## What this rules in and out

- **Not network placement.** With the partition, processes and workers
  identical, moving half the processes to a second node changes work by
  −3% to +8% and reduces time by 4–12%.
  - Offered inter-node attempts are only 0.16–0.19%.
  - This agrees with the Delta observation that per-attempt cost stays flat.
- **Not the number of priority domains or partitions.** At nearly constant
  total workers, 16 × 7 and 8 × 15 do about the same work (−2% to +8%).
- **Global concurrency, not concurrency within each domain.**
  - D halves the workers per domain at fixed total concurrency, and work does not fall.
  - W doubles total concurrency at fixed domains, and work rises 60–84% on road.
  - The growth follows how many workers run at once, relative to the useful
    wavefront width, not how they are grouped.
  - This is the familiar speculative-work problem of asynchronous SSSP: with
    more active workers than the thin frontier supports, extra workers expand
    vertices at distances that later improve. Expansions per vertex rise
    from 3.0 to 5.1, while stale pops stay at 9%.
- **Independent corroboration:** training-selected GAPBS also uses only
  **64 threads** on road, against 128 on mesh26 and 120 on mesh24.

## Consequences for the plan

1. **The one-node ACIC baseline is not at its best layout.**
   - 16 × 7 on one node is **8–20% faster than 8 × 15** on both graphs and in
     both allocations, with about the same work.
   - Per-attempt solver cost is lower: road 280 vs 358–370 ns, mesh 220 vs 274–289 ns.
   - The lower cost comes with less lock missing and fewer entries per removal.
   - Any C6 or scaling denominator must use the best layout at each node count,
     chosen on training sources, for ACIC as for GAPBS.
2. **Best-to-best scaling from one node to two** (best of 8 × 15 and 16 × 7 at one node):
   - mesh improves **1.24–1.28×**, with 2-node 8 × 15 best;
   - road improves **1.04–1.14×**, with 2-node 8 × 7 best.
   - On road, 240 workers are slower than 112 in both allocations, by 11–28%
     per source.
   - Road therefore has no credible scaling path by adding workers under the
     current policy.
3. **Both earlier interventions were aimed at the wrong target.**
   - Tighter queue priority (R1 nearest) and larger batches change ordering
     within a domain.
   - The measured cause is global speculative breadth, which neither changes.
   - This explains why nearest priority reduced one-node work but did not fix
     eight-node road.

## Selected next intervention (proposed, not implemented)

Following the recorded rule for W ("target breadth at a strong fixed setting
before any live controller"):

- **Strong fixed baseline (no code):** the best layout per node count, chosen
  on training sources.
  - Today that is 16 × 7 at one node, 8 × 7 per node on road at two nodes,
    and 8 × 15 per node on mesh.
  - A per-graph layout table is tuning, not a mechanism, and is not a paper claim.
  - Every later candidate must beat this baseline, not 8 × 15.
- **Intervention:** a single concurrency limit, the admission window.
  - Workers take new queue work only while the global histogram shows enough
    admissible work per active worker.
  - Otherwise they park, while the other workers keep going.
  - This limits speculative breadth without giving up cores permanently.
  - Its setting comes either from graph metadata when the graph is read
    (C3b), or live from the observed rework rate (C3c).
  - It is exactly the author's sense of adaptivity: real-time control of how
    much work flows.
  - Existing L3 slack regulated how far ahead a PE runs, not how many workers
    are active. It was tested only before batching and nearest priority.
    Reusing its accounting is an implementation choice to evaluate.
- **Prediction:** at 2 nodes × 8 × 15 on road, work returns to within 15%
  of the 8 × 7 level. Time is at least 10% below the fixed 8 × 7 layout, or
  the controller adds nothing beyond choosing a layout. Mesh is not slower
  than 8 × 15 by more than the noise floor.
- **Disconfirming result:** the limit matches or loses to the fixed 8 × 7
  layout on road, or costs mesh time. In that case the mechanism has no
  contribution beyond layout selection, and the plan's stop rule applies to
  this route.

Before any 4/8-node pilot, a small fixed-layout scaling check (1/2/4 nodes,
7 vs 15 workers per process) would show whether road's optimum keeps falling
in workers per node as nodes grow. If it does, eight-node road cannot beat one
node without breadth control.

## Evidence

- [Allocation A audit](onenode-data/r1-attrib-anvil-20833718.json)
- [Allocation B audit](onenode-data/r1-attrib-anvil-20833719.json)
- [Gate manifest](onenode-data/r1-attrib-verify-anvil-20833717-manifest.json)

Raw logs are under `/anvil/scratch/x-rrao/acic/ipdps27-onenode/logs/`.
