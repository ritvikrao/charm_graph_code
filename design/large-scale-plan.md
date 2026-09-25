# Larger inputs and node counts: road and mesh (at most 64 nodes)

Goal: ACIC against every applicable baseline on road and mesh graphs larger
than the 16-node campaign's, at 4, 16, 32 and 64 Frontier nodes. The user's
ceiling is 64 physical nodes.

## Inputs

| Graph | Vertices | Edges | Max distance | One node? | Baselines |
|---|---:|---:|---:|---|---|
| road-usa-z (DIMACS) | 23.9M | 57.7M | 54.4M | yes | GAPBS, Wasp, Gluon (RIKEN: distances past 2^24) |
| road-na-z (OSM 2026-01-01) | 62.1M | 155.0M | 9.4M m | yes | GAPBS, Wasp, Gluon, RIKEN |
| road-eu-z (OSM 2026-01-01) | 95.6M | 228.7M | 6.7M m | yes | GAPBS, Wasp, Gluon, RIKEN |
| mesh24-z | 16.8M | 67.1M | 0.94M | yes | GAPBS, Wasp, Gluon, RIKEN |
| mesh26-z | 67.1M | 268.4M | 2.0M | yes | GAPBS, Wasp, Gluon, RIKEN |
| mesh28-z | 268M | 1.07B | ~4M (expected) | yes | GAPBS, Wasp, Gluon, RIKEN |
| mesh30-z | 1.07B | 4.29B | ~8M (expected) | yes (int32 ids, 43 GB file) | GAPBS, Wasp, Gluon, RIKEN |

mesh28-z and mesh30-z are written directly in Morton order by
`prepare_graph meshz`, byte-identical to gen + `reorder_graph.py` on mesh24-z
and mesh26-z; references come from `reference_gap` (sources in parallel,
identical output). RIKEN is exact only below 2^24, which `reference_gap`'s
guard checks when the reference is built.

Past one node (not scheduled here): mesh32-z (4.3B vertices, 17.2B edges)
runs on ACIC's `WIRE=compact64` build with `--certify`; Gluon needs 64-bit-id
Galois files and RIKEN a wide `.wsg` adapter first.

## Matrix

| Series | Points | Baselines in the allocation |
|---|---|---|
| Road, strong | road-usa-z, road-na-z, road-eu-z at 4, 16, 64 nodes | Gluon-Async (full search; 8 ranks per node, oec pinned at 4 nodes), RIKEN on road-na/eu (inexact, separate jobs) |
| Mesh, weak (~4.2M vertices/node) | mesh24-z@4, mesh26-z@16, mesh28-z@64 | Gluon-Async, RIKEN |
| Mesh, strong | mesh26-z@64; mesh28-z@16; mesh30-z@32, @64 | Gluon-Async, RIKEN (pinned on mesh28/30) |
| One node | every graph | GAPBS and Wasp, 56 threads, delta tuned on training sources |

ACIC arms in every allocation: `frozen` (production `acic_slice`) and `tls`
(`acic_tls`, 7afd94d on the initial-exec TLS runtime). Road widths: 131072 on
road-usa-z; road-na-z and road-eu-z selected on training sources at 16 nodes
(job 5541364, 96 valid solves): 131072 on both. The surface is flat, 8K-128K
within 2% (0.336-0.342 s road-na-z, 0.403-0.407 s road-eu-z), because bucket
coarsening brings 8K and 32K up to the same effective width; every width has a
1.30-1.42x speedup over plain. Predictions: the fastest width missed (131072,
above the predicted 16K-32K and 8K-32K, though every width is within 2%); the
1.3x-over-plain prediction was met. Gluon-Sync is left out: on mesh24/26 it
was 20-40x slower than Async in every tuning run.

Pinned baseline settings, where a full search does not fit two hours: Gluon on
mesh28/30 uses 8 ranks per node, oec, delta 64 (selected on mesh24-z and
mesh26-z at 16 nodes in both allocations); RIKEN uses 8 ranks per node, delta
1024 (selected on mesh26-z at 16 nodes, job 5536476).

## Stages (allocation A)

| Stage | Jobs | Notes |
|---|---|---|
| 1 | 5541369 (4n), 5541370 (16n), 5541371 (64n), all done | mesh24/26-z, road-usa-z |
| 2 | 5541426 (4n, cancelled after ACIC), 5541428 (16n), 5541429 (64n), done | road-na-z, road-eu-z |
| 64-node ACIC rerun | 5541660 | 5541371 and 5541429 lost their ACIC arms: every 512-process launch aborted in Cray PMI (`_pmi2_add_kvs`, LCI's bootstrap needs ranks^2 KVS entries; the harness pinned 100000). Fixed in cf42624; their Gluon and RIKEN runs are valid |
| 2, 4-node rerun | 5541712 (ACIC + Gluon-Async) | 5541426 was cancelled at 40 min: 60-85 s Gluon solves and RIKEN timeouts left no room for the search. ACIC in 5541426 finished (64 valid rows). The rerun pins Gluon to 8 ranks per node, oec (every 16- and 64-node search chose it) and searches deltas 0, 131072 and 2097152, which the 16- and 64-node runs split between |
| RIKEN on OSM roads | 4n: 5541713 (NA), 5541714 (EU, 2 sources x 1 rep); 16n: 5541715 (NA), 5541716 (EU, 2 reps); 64n: 5541717 (both, 2 reps) | Every RIKEN run in stage 2 was inexact or timed out: all vertices reached, the distance sum high by 2.6e-5 (NA) and 5.9e-5 (EU), at every layout and node count. RIKEN aims at Graph500 accuracy (validate.hpp, relative 1e-5 per edge). The "hangs" were launch timeouts at 330 s on slow layouts (the 8-rank layout took 358 s at 4 nodes). These jobs run with `run.py --riken-tolerance 1e-3` (every vertex reached, distance sum high by at most 0.1%; recorded `exact=false` with its error), 8 and 56 ranks per node (56 only at 4 nodes), delta 131072 |
| 3 | 5541663 (mesh28-z@16), 5541664 (@64); mesh30-z@32: 5541665 (ACIC + Gluon), 5541666 (RIKEN); @64: 5541667, 5541668 | pinned Gluon/RIKEN; mesh30-z at two repetitions |
| One node | GAPBS 5541358 / Wasp 5541359 (road-na/eu); 5541661 / 5541662 (mesh28/30) | 56 threads |

Inputs: mesh28-z (max distance 3.8M) and mesh30-z (6.9M) from job 5541357,
both below 2^24, so RIKEN is exact on both.

Allocation B (a repeat of every point) is submitted only if the user asks, after A is reported. Estimated cost about 700
node-hours per allocation.

## Results, allocation A (complete 2026-09-25)

Speedup = baseline time / ACIC time, per held-out source (median over
repetitions), range over sources; `tls` is the TLS build, production in
parentheses. GAPBS and Wasp are one node, 56 threads, delta tuned per graph.
Full per-cell output: design/onenode-data/frontier-large-A-summary.txt
(benchmarks/large_summary.py). No ACIC run was invalid, stalled or rescued.

| Graph @ nodes | ACIC tls (s) | over Gluon | over RIKEN | over GAPBS | over Wasp |
|---|---:|---:|---:|---:|---:|
| mesh24-z @4 | 0.12-0.13 | 28-73x (23-58) | 68-87x (54-71) | 0.61-0.64x (0.49-0.52) | 0.53-0.64x (0.44-0.51) |
| mesh26-z @16 | 0.18-0.22 | 61-72x (49-57) | 94-206x (76-169) | 1.55-1.74x (1.24-1.41) | 1.20-1.50x (0.95-1.22) |
| mesh26-z @64 | 0.14-0.18 | 55-63x (44-51) | 65-113x (54-90) | 1.94-2.29x (1.55-1.91) | 1.52-1.98x (1.23-1.65) |
| mesh28-z @16 | 0.60-0.69 | 76-97x (61-78), 2 sources | 580-772x (467-625), 2 sources | 2.19-2.64x (1.74-2.11) | 1.32-1.61x (1.04-1.29) |
| mesh28-z @64 | 0.33-0.41 | 72-105x (59-86) | 292-389x (241-322) | 3.70-4.79x (3.07-3.95) | 2.23-2.92x (1.85-2.41) |
| mesh30-z @32 | 1.32-1.58 | 95-134x (77-107) | none: > 1590 s timeout | 4.40-4.73x (3.50-3.80) | 2.07-2.56x (1.65-2.05) |
| mesh30-z @64 | 0.89-1.12 | 85-192x (68-157) | none: 1024-1469 s on training sources | 6.16-7.04x (4.95-5.73) | 2.90-3.80x (2.32-3.10) |
| road-usa-z @4 | 0.18-0.23 | 34-123x (28-103) | n/a (> 2^24) | 0.61-0.67x (0.50-0.55) | 0.33-0.40x (0.27-0.33) |
| road-usa-z @16 | 0.12-0.17 | 32-72x (26-60) | n/a | 0.86-1.00x (0.73-0.84) | 0.47-0.60x (0.40-0.50) |
| road-usa-z @64 | 0.10-0.15 | 24-52x (22-47) | n/a | 0.97-1.18x (0.88-1.05) | 0.53-0.70x (0.48-0.62) |
| road-na-z @4 | 0.49-0.59 | none (5541712 failed) | 294-1076x (238-889), inexact | 1.15-1.41x (0.95-1.14) | 0.49-0.65x (0.41-0.53) |
| road-na-z @16 | 0.32-0.41 | 27-98x (23-79) | 163-496x (131-414), inexact | 1.66-2.17x (1.38-1.76) | 0.71-1.01x (0.59-0.82) |
| road-na-z @64 | 0.28-0.38 | 15-46x (13-38) | 82-239x (68-197), inexact | 1.77-2.45x (1.49-2.05) | 0.75-1.14x (0.63-0.95) |
| road-eu-z @4 | 0.59-0.71 | none (5541712 failed) | 432-2179x (358-1808), inexact, 2 sources | 0.98-1.07x (0.81-0.89) | 0.50-0.64x (0.42-0.53) |
| road-eu-z @16 | 0.31-0.41 | 18-135x (15-110) | 286-1187x (234-965), inexact | 1.68-2.07x (1.36-1.69) | 0.86-1.24x (0.70-1.01) |
| road-eu-z @64 | 0.27-0.36 | 10-61x (8-51) | 126-537x (105-443), inexact | 1.90-2.39x (1.57-1.99) | 0.98-1.43x (0.81-1.19) |

RIKEN on the OSM roads: every vertex reached, distance sum high by
3.0e-5-3.7e-5 (NA) and 4.9e-5-6.4e-5 (EU). The TLS build is 1.19-1.26x over
production everywhere except road-usa-z @64 (1.10-1.13x).

Gaps: road-na/eu-z @4 have no Gluon (5541712 was submitted without
VARIANTS_PREFIX=frontier-large and failed in 6 s); mesh28-z @16 Gluon and
RIKEN cover 2 of 4 sources (5541663 died on a Lustre EIO writing its log;
5541713 died the same way after 6 of 8 held-out runs); mesh30-z RIKEN has no
held-out time (32 nodes: two launches past 1590 s; 64 nodes: 1024-1469 s per
solve, then a timeout). The RIKEN layout stage on mesh30-z ran the mid delta
(64), not the pinned 1024: `--delta-divisors` sets only the parameter grid.
