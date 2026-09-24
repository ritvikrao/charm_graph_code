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
| Road, strong | road-usa-z, road-na-z, road-eu-z at 4, 16, 64 nodes | Gluon-Async (full search), RIKEN on road-na/eu (full search) |
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

## Stages

1. Ready now: mesh24-z@4, mesh26-z@16 and @64, road-usa-z@4, @16, @64.
2. After width selection: road-na-z, road-eu-z at 4, 16, 64.
3. After preparation (job 5541357): mesh28-z@16, @64; mesh30-z@32, @64;
   GAPBS and Wasp on mesh28-z and mesh30-z.

Allocation A first; allocation B repeats every point once A is checked.
Estimated cost about 700 node-hours per allocation.
