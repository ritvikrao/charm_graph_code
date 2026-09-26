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

## Real inputs past the synthetic ones (2026-09-25)

| Input | Size | Baselines |
|---|---|---|
| `road-planet-z`: OSM planet 2025-12-29 (`scripts/frontier/fetch_planet.sh`, md5-checked), RoutingKit car graph, largest component, Morton by coordinates | 199,497,757 vertices, 495,996,636 edges; max distance 13.0M-18.0M m | GAPBS, Wasp (one node), Gluon; RIKEN excluded (distances past 2^24) |
| `terrain-ae-z`: Copernicus DEM GLO-90, land south of 50N, 8-neighbour Tobler walking time in deciseconds (cap 36000), component containing the Tian Shan (Africa joins through Sinai), Morton ids (`benchmarks/terrain_graph.cpp`) | 8,196,328,992 vertices, 65,560,096,540 edges, 1.11 TB (wide ids); max distance 64M-113M | Gluon via `gluon64.patch` (oec only); no one-node baseline fits; RIKEN excluded |

`terrain-ae-z` references come from ACIC `--certify` at 16 nodes (job 5546087,
all six sources certified; 34.6-44.5 s per solve after a 179 s read).

| Stage | Jobs | Notes |
|---|---|---|
| Inputs | 5545069 (planet extraction), 5545070 + 5546077 (planet files; the first stopped at the binary32 reference guard), 5545904 (terrain build, debug QOS), 5546090 (terrain Galois version-2 copy) | |
| Gluon 64-bit check | 5545185 | passed (current-state §14) |
| Terrain | ACIC 8n 5546130, 16n 5546131 (done); Gluon delta pilot 64n 5546129 (deltas 8192, 1024, 65536 on one tuning source) | pilot never eligible: 5546090's copy timed out at 607 of about 850 GB; Gluon held-out at 32n and 64n after the pilot, with `run.py --external-no-search` |
| Planet | GAPBS 5546133, Wasp 5546134; ACIC + Gluon-Async 4n 5546135 (Gluon pinned 8 ranks, oec), 16n 5546136, 64n 5546137 (done) | GAPBS and Wasp crashed at start (`run.py` regression, fixed in 9028c4d), to rerun; 5546135 hit its limit on Gluon's last repetition |

## Results, allocation A (complete 2026-09-25)

The results are in design/current-state.md, § Results by dataset (every
dataset, node count and implementation, generated by
`benchmarks/results_by_dataset.py`); the allocation-A-only summary is
design/onenode-data/frontier-large-A-summary.txt.

Gaps: road-na/eu-z @4 have no Gluon (5541712 was submitted without
VARIANTS_PREFIX=frontier-large and failed in 6 s); mesh28-z @16 Gluon and
RIKEN cover 2 of 4 sources (5541663 died on a Lustre EIO writing its log;
5541713 died the same way after 6 of 8 held-out runs); mesh30-z RIKEN has no
held-out time (32 nodes: two launches past 1590 s; 64 nodes: 1024-1469 s per
solve, then a timeout). The RIKEN layout stage on mesh30-z ran the mid delta
(64), not the pinned 1024: `--delta-divisors` sets only the parameter grid.

## Results, real inputs (2026-09-26)

Tables: design/current-state.md, `road-planet-z` and `terrain-ae-z` under
§ Results by dataset (generated by `benchmarks/results_by_dataset.py`). All
solves counted returned the reference digest; the terrain references are
certified.

| Input | Nodes | ACIC TLS (s) | ACIC production (s) | Gluon-Async (s) | TLS speedup over Gluon |
|---|---:|---:|---:|---:|---:|
| `road-planet-z` | 4 | 1.78–1.97 | 2.15–2.37 | 253–474 | 141–244× |
| `road-planet-z` | 16 | 0.87–1.06 | 1.05–1.26 | 91–165 | 95–172× |
| `road-planet-z` | 64 | 0.63–0.83 | 0.74–0.98 | 31–61 | 44–85× |
| `terrain-ae-z` | 8 | 44.9–57.3 | 57.9–73.7 | — | — |
| `terrain-ae-z` | 16 | 26.4–34.7 | 34.2–44.6 | — | — |

Against the recorded predictions:
- **Planet** (reusing road-usa-z's): TLS over production 1.15–1.24× (predicted
  1.10–1.30×), met. 4 → 16 nodes 1.86–2.05× (predicted 1.2–2×), at the edge.
  16 → 64 nodes 1.27–1.41× (predicted within 0.8–1.3× of 16), better than
  predicted. Speedup over Gluon 36–244× (predicted 15–80×), above.
- **Terrain:** TLS over production 1.29–1.30× (predicted 1.10–1.30×). 8 → 16
  nodes 1.63–1.70× (predicted 1.5–2.0×). Both met.

Gaps and fixes before the next allocation (none submitted):
- Terrain Gluon needs the version-2 copy finished. Frontier limits jobs under
  92 nodes to two hours, and `to_galois.py` wrote at about 84 MB/s, so the
  copy needs parallel or resumable writing.
- The 64-node Δ pilot 5546129 is stuck in DependencyNeverSatisfied.
- The planet one-node GAPBS and Wasp tuning must be rerun with the fixed
  harness.

