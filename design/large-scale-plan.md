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


## Added inputs (2026-09-26, preparation submitted)

The paper focuses on large non-scale-free graphs, at most 64 nodes. Added:

| Input | Size | Built by | Baselines | Jobs |
|---|---|---|---|---|
| `terrain30-s-z`: GLO-30, lat [0, 12) × lon [21, 34), seed 6N 27E | 2,021,760,000 vertices (below 2^31, narrow) | `terrain_graph2 --arcsec 1 --narrow` | GAPBS, Wasp (one node), Gluon; RIKEN if distances stay below 2^24 | build 5551477, reference (one-node Dijkstra) 5551486 |
| `terrain30-m-z`: lat [-6, 18) × lon [12, 40), same seed | 8,654,297,795 vertices | `terrain_graph2 --arcsec 1` | Gluon (`gluon64.patch`) | build 5551478, certified reference at 16 nodes 5551487 |
| `terrain30-l-z`: lat [-35, 32) × lon [-18, 52), same seed (Africa, Arabia, the Levant) | 34,347,781,457 vertices | `terrain_graph2 --arcsec 1`, six parts | Gluon | parts 5551479–5551484, finalize 5551485, certified reference at 64 nodes 5551488 |
| `mesh32-z` | 4,294,967,296 vertices, 17.2B edges | `grid_graph 2` (wide) | Gluon | build 5551360, certified reference 5551362 |
| `grid3-30-z`: 1024^3, six neighbours | 1,073,741,824 vertices, 6.4B edges | `grid_graph 3` | all | 5551359 (prepare_inputs) |
| `grid3-33-z`: 2048^3 | 8,589,934,592 vertices, 51.5B edges | `grid_graph 3` (wide) | Gluon | build 5551361, certified reference 5551363 |
| `mesh28-w10-z`, `mesh28-w64k-z`: mesh28-z's topology and ids, weights uniform over [1, 10] and [1, 65536] | as `mesh28-z` | `grid_graph 2` | all (RIKEN not on w64k: distances past 2^24) | 5551359 |

The terrain crops nest around one seed and hold 505M, 541M and 537M vertices
per node at 4, 16 and 64 nodes (terrain-ae-z: 512M at 16), from component
counts in job 5551356 (`scripts/frontier/terrain_series_count.sbatch`).
GLO-30 tiles: `scripts/frontier/fetch_glo30.sh`, 3,230 tiles. `terrain_graph2`
(7dc7f2b) is byte-identical to `terrain_graph` at 3 arc-seconds and matches the
independent `benchmarks/check_terrain.py` exactly at 1 arc-second. `grid_graph`
(517b86b) 2-D output is byte-identical to `prepare_graph meshz`. Both write the
Galois `.gr` directly. terrain-ae-z's `.gr` is rewritten the same way (5551364)
after `to_galois.py` ran past the time limit.

## Scaling series (2026-09-28, submitted)

Node counts 4, 16, 32 and 64 (32 added at the user's request), never more
than 64. Preparation results: every build matched its builder counts and
sources; references certified (mesh32-z, grid3-33-z, terrain30-m-z at 16
nodes) or one-node Dijkstra (mesh28-w10-z, mesh28-w64k-z, grid3-30-z). Three
failures, fixed:
- terrain-ae-z `.gr` (5551364): the file was complete in 19 min; the
  million-entry spot check timed out. A windowed check (128,000 offsets and
  edges) passed on the login node and the file was installed (dd653b6).
- terrain30-s-z reference (5551486): `sbatch --wrap` ran /bin/sh, which has no
  `<(...)`. Resubmitted as `scripts/frontier/reference_gap.sbatch` (5558144).
- terrain30-l-z reference at 64 nodes (5551488): PE 0 held the whole offsets
  array and a tiled copy, 2 x 275 GB, OOM. The reader now finds each PE
  boundary by binary search over offsets read on demand
  (`graphlib/edge_partition.h`, 29f8d6a); identical boundaries on 1128
  layouts (`tests/test_edge_partition.cpp`). Built as `acic_scale64`
  (29f8d6a, TLS runtime, compact64), checked against five existing references
  on one node (5558176), then the l-z reference at 64 nodes (5558177).

Also fixed: `gridz()` wrote the build log's first line as `.meta`, so the three
synthetic variants had no `riken_denominator` (run.py needs it); rewritten.

| Jobs | What |
|---|---|
| 5558200 / 5558201 / 5558202 / 5558203 | ACIC, 4 / 16 / 32 / 64 nodes: terrain30-s-z, terrain30-m-z (16+), mesh32-z, grid3-33-z (16+), mesh28-z, mesh28-w10-z, mesh28-w64k-z, grid3-30-z (`frontier-series-mesh-<N>n-variants.json`, predictions recorded) |
| 5558204 / 5558205 | ACIC terrain30-l-z, 32 / 64 nodes |
| 5558207–5558214, 64n-l resubmitted (see below) | Gluon-Async pinned (`series_gluon.sbatch`: 8 ranks, oec, Δ 64 on [1, 1000] weights, 1 on w10, 4096 on w64k, 128 on terrain), 1–2 held-out sources, one repetition, capped launches; terrain30-l-z at 64 nodes only |
| 5558223 / 5558224 | GAPBS / Wasp one node (56 threads, Δ tuned): mesh28-z, mesh28-w10-z, mesh28-w64k-z, grid3-30-z |
| 5558225 / 5558226 | GAPBS / Wasp one node on terrain30-s-z (Δ 512–32768, two repetitions) |

Revision before any series job ran: 29f8d6a's on-demand probes were
latency-bound (about 20 ms per uncached Lustre read, one at a time), so
mesh28-z indexed in 26.9 s against 3.1–4.3 s before (5558176). a1970e4
gallops from the previous boundary before bisecting and runs a tiled layout's
owners and tile starts on up to 64 threads (acic_scale64b). Checked on one
node (5558304): same five references match; index time mesh28-z 3.96 s,
grid3-30-z 14.3 s (60.7 s with 29f8d6a), mesh28-w64k-z 15.7 s. The series
jobs now use acic_scale64b and wait on 5558304; the terrain30-l-z reference is
5558305 (5558177 cancelled), and its Gluon job is 5558382 (5558215 was
cancelled with it).

## Gemini and HavoqGT, phase A (2026-09-28, submitted)

Builds (d3ec098, b73110f): Gemini 170e7d3 + `benchmarks/gemini.patch` (uint32
weights, uint64 distances, sources list, solve-only timer, digest, NUMA fix,
64-bit ids with `-DGEMINI_VERTEX64`); HavoqGT master 2e8b2a8 +
`benchmarks/havoqgt.patch` (Metall v0.29, uint32 weights, exact source, digest;
an MPI_Abort no longer hangs in MPI_Finalize). Smoke 5558543 and 5560036:
every digest correct on road-ny, mesh20 and mesh24-z at 1 and 2 nodes, and on
the 4.4e9-id strided mesh20 (gemini_sssp64, HavoqGT). mesh24-z per solve:
Gemini 18–41 s (1–2 nodes), HavoqGT 39–73 s (1–2 nodes).

Phase A: mesh28-z, mesh28-w10-z, mesh28-w64k-z, grid3-30-z (2 held-out
sources, launch cap 700 s) and terrain30-s-z (2 sources, cap 1800 s) at 4, 16,
32 and 64 nodes; Gemini 1 rank x 56 threads per node, HavoqGT 56 ranks per
node; `scripts/frontier/series_baseline.sbatch` via run.py
`--external-no-search`. Inputs: `convert_gemini.sbatch` 5560446. Jobs:
Gemini 5560447–5560454, HavoqGT 5560455–5560462 (small, sz per node count).
A capped launch gives a lower bound on ACIC's speedup.

## Results, scaling series (complete 2026-09-28)

Tables: design/current-state.md, § Results by dataset (`mesh28-z`, `mesh32-z`,
the weight-range variants, the 3-D grids, the GLO-30 terrain series). All 32
ACIC cells have four held-out sources, one warmup and three repetitions, and
every counted solve returned the reference digest. Two cells lost a launch on
the first attempt and reran the arm alone in the same job:
- `grid3-33-z`@32: a network error (CXI service teardown).
- `terrain30-m-z`@64: a `PROGRESS_STALL` that recovered with the right answer
  in 22.4 s against about 10 s; the only stall in the series.

| Input | 4 nodes (s) | 16 (s) | 32 (s) | 64 (s) | Gluon speedup range |
|---|---:|---:|---:|---:|---:|
| `terrain30-s-z` | 17.3–18.1 | 5.85–6.18 | 3.49–3.90 | 2.25–2.62 | 129–228× |
| `terrain30-m-z` | — | 25.6–29.1 | 15.4–17.9 | 9.69–11.8 | ≥ 104–171× (capped) |
| `terrain30-l-z` | — | — | 66.6–72.1 | 43.3–45.7 | ≥ 119× (capped) |
| `mesh32-z` | 27.0–30.0 | 9.08–10.3 | 5.65–6.30 | 3.61–4.14 | ≥ 93–167× (capped); 202× |
| `grid3-33-z` | — | 16.0–17.4 | 8.49–9.16 | 4.66–5.16 | 9.9–25.8× |
| `grid3-30-z` | 6.90–7.28 | 2.15–2.22 | 1.21–1.28 | 0.707–0.767 | 7.7–18.7× |
| `mesh28-z` | 1.67–1.74 | 0.599–0.696 | 0.412–0.482 | 0.327–0.408 | 72–112× |
| `mesh28-w10-z` | 1.95–2.01 | 0.792–0.864 | 0.636–0.701 | 0.619–0.668 | 37–90× |
| `mesh28-w64k-z` | 1.67–1.79 | 0.602–0.704 | 0.405–0.471 | 0.321–0.403 | 75–116× |

One node: GAPBS and Wasp beat ACIC@4 on `terrain30-s-z` (0.72–0.95×); ACIC
passes them at 16 nodes (2.2–2.8×) and reaches 5.6–7.0× at 64.

Against the recorded predictions (`frontier-series-mesh-<N>n-variants.json`):
- Every solve returns the reference digest: met.
- `terrain30-m-z`@16 22–40 s: met (25.6–29.1 s).
- Weak scaling along the terrain series 1.5–3× per step: met at the low edge
  (1.4–1.8×).
- Strong scaling on inputs of 2B+ vertices:
  - 4 → 16 nodes 1.8–3.0×: slightly above (`terrain30-s-z` 2.83–3.08×,
    `mesh32-z` 2.88–3.09×).
  - 16 → 32 nodes 1.3–1.9×: met (1.54–1.94×).
  - 32 → 64 nodes 1.2–1.8×: met (1.47–1.84×).
- `mesh32-z`@64 2–6 s: met (3.61–4.14 s).
- Speedup over Gluon-Async 30–200× wherever Gluon finishes: met on the 2-D
  meshes (37–116×). Above the range on `terrain30-s-z` (129–228×). Missed on
  the 3-D grids (7.7–26×), whose low diameter suits Gluon's rounds.
- `mesh28-w10-z` within 0.7–1.3× of `mesh28-z`: met at 4 and 16 nodes, missed
  at 32 (1.46–1.54×) and 64 (1.64–1.89×), where w10 stops scaling.
- `mesh28-z` time / `mesh28-w64k-z` time 0.4–1.0: met (0.97–1.03; no cost).

Open: Gemini and HavoqGT phase A (submitted); why narrow weights stop ACIC's
scaling; terrain-ae-z Gluon and ACIC at 32/64 nodes.

## Results, Gemini/HavoqGT phase A and O1/O2 (complete 2026-09-29)

All 16 phase A jobs (5560447–5560462) and the reviewer-driven O1/O2 jobs
(5561481–5561486) finished. The results are in current-state §31 and the
regenerated dataset tables, which now carry Gemini and HavoqGT columns.
- ACIC is faster than both baselines on every input and node count. HavoqGT
  is the strongest distributed baseline on `terrain30-s-z` (17–24×). Gemini
  hits its cap on every 2-D mesh launch but one.
- On RMAT at 16 nodes ACIC is 1.35–2.59× faster than Gemini and 4.3–12×
  faster than HavoqGT. HavoqGT here runs without delegates: the upstream
  threshold makes none, and with threshold 896 every solve hung (O2b probes
  this).
- Vertex order: row-major `mesh26` costs ACIC 107–183× at 16 nodes, and Gluon
  wins there. DIMACS-ordered `road-usa` costs ACIC 20–28×, and ACIC still
  wins by 38–61×. O1b tests reader tiling as the cause.

Phase B is dropped. The follow-up Frontier batch (F4–F10, O1b, O2b) is
tracked in sc27-plan.md.
