# ACIC current evidence

*Status 2026-09-25. Results are grouped by dataset, then node count, then
implementation (§ Results by dataset); the mechanism sections that follow keep
only the measurements specific to each mechanism. Raw summaries remain under
`design/onenode-data/`; job logs remain in the recorded campaign directories;
earlier narratives remain in Git history.*

*Reporting rule (2026-09-24): every comparison is a speedup, the reference
time divided by ACIC's (or the named arm's) time. Above 1× means ACIC is
faster; below 1× means it is slower.*

## Executive conclusion

**Against distributed SSSP.** ACIC is faster than tuned Gluon (D-Galois) on
every graph, node count and held-out source measured: 1.5–5.3× on RMAT
25–27, 8.9–17× on `uniform25` and orkut, and 8–192× on meshes and roads
(4–64 nodes). It is faster than RIKEN on every mesh and road (54–2,179×;
RIKEN's answers on the OSM roads are slightly inexact), and slower than RIKEN
on every scale-free graph (0.29–0.90×).

**Against one-node shared-memory SSSP.** On meshes, ACIC at 16 nodes or more
beats tuned GAPBS from `mesh26-z` up, and Wasp on every source except one
`mesh26-z` source at 16 nodes (production 0.97×); the margin grows with size:
at 64 nodes 1.6–2.3× over GAPBS and 1.2–2.0× over Wasp on `mesh26-z`, and
5.0–7.0× and 2.3–3.8× on `mesh30-z`. On the quarter-size `mesh24-z` it only
reaches parity at 16 nodes. On roads, ACIC at 16–64 nodes beats GAPBS on the
OSM roads (1.4–2.5×) and roughly ties Wasp (0.59–1.43×). On the smaller DIMACS
`road-usa-z` it at best ties GAPBS (0.97–1.18× at 64 nodes, TLS) and stays
well behind Wasp (0.27–0.70×). Road time barely falls beyond 16 nodes.

**Builds.** The TLS builds are 1.1–1.3× faster than production on meshes and
roads (cheaper rounds; the hub hints resolve off), 1.0–1.1× on orkut and
`uniform25`, and 1.2–1.5× on RMAT, where the hints act.

**Not yet measured.** Road and mesh inputs past one node's memory: the OSM
planet road graph (`road-planet-z`, 199.5M vertices, fits one node) and the
Copernicus terrain graph (`terrain-ae-z`, past 2^32 vertices; Gluon needs
`gluon64.patch`) are being prepared.

## Implementations

| Name in the tables | Build | Settings |
|---|---|---|
| ACIC production | `acic_slice` (5ab6d5b) | 8 processes × 7 workers per node (4 × 14 where named), process-wide nearest queue, batch 8, heap slice 8, `+old-scheduler`; roads add `--bucket-width 131072` |
| ACIC TLS (acic_hint2_tls) | hints v2 working tree on the initial-exec TLS runtime | production settings plus `--hub-hints auto` (Gluon study, jobs 5539286–5541240) |
| ACIC TLS (acic_tls) | 7afd94d on the initial-exec TLS runtime | production settings plus `--hub-hints auto` (larger-input study, jobs 5541369 onward) |
| GAPBS | `gap_sssp`, one node | 56 threads, Δ tuned on the training sources |
| Wasp | SC25 artifact `wasp_sssp`, one node | 56 threads, Δ tuned on the training sources |
| Gluon | D-Galois `sssp-push` (`gluon.patch`: timer and digest only) | ranks per node, partition (oec/cvc) and Δ tuned on training sources per mode; Async and Sync on scale-free, Async only on mesh and road (Sync was 20–40× slower); pinned to 8 ranks, oec, Δ 64 on `mesh28-z`/`mesh30-z` |
| RIKEN | Graph500 SSSP (552f156) | ranks per node and Δ tuned on training sources; exact only while distances stay below 2^24, so absent on `road-usa-z`; on the OSM roads accepted within `run.py --riken-tolerance 1e-3` and marked inexact |

Every row is the physical number of Frontier nodes. All times are held-out
sources (four per graph, drawn by `reference_gap`), never the training sources
the settings were tuned on.

## Results by dataset

Generated from the raw logs by `benchmarks/results_by_dataset.py`
(`design/onenode-data/results-by-dataset.json`). Time: per held-out source,
the median over repetitions and allocations; the table gives the range over
sources. Speedup: baseline time / ACIC time per source, range over sources.
Where ACIC and a baseline ran in the same job the pair is taken within each job
and the ranges are joined; otherwise (one-node references, and the 64-node
cells whose ACIC arms ran in the separate job 5541660) per-source medians are
paired across jobs. Gluon is the faster of Async and Sync per source where
both ran. Only valid solves count; every solve counted returned the reference
digest.

### Meshes (Morton-ordered 2-D grids)

#### `mesh24-z` (16.8M vertices, 67.1M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 1 | ACIC production | 0.391–0.428 | 4 | 5538412, 5538413 |
| 1 | GAPBS | 0.074–0.080 | 4 | 5538410 |
| 1 | Wasp | 0.068–0.081 | 4 | 5538411 |
| 2 | ACIC production | 0.239–0.251 | 4 | 5538412, 5538413 |
| 4 | ACIC production | 0.143–0.159 | 4 | 5538412, 5538413, 5541369 |
| 4 | ACIC TLS (acic_tls) | 0.115–0.128 | 4 | 5541369 |
| 4 | Gluon (Async) | 3.54–9.28 | 4 | 5541369 |
| 4 | RIKEN | 7.82–11.1 | 4 | 5541369 |
| 8 | ACIC production | 0.092–0.112 | 4 | 5538412, 5538413 |
| 16 | ACIC production | 0.073–0.094 | 4 | 5538412, 5538413, 5541196, 5541240 |
| 16 | ACIC TLS (acic_hint2_tls) | 0.059–0.076 | 4 | 5541196, 5541240 |
| 16 | Gluon (Async) | 3.13–4.29 | 4 | 5541196, 5541240 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 1 | ACIC production | 0.18–0.20× | 0.16–0.20× | — | — |
| 2 | ACIC production | 0.31–0.33× | 0.28–0.33× | — | — |
| 4 | ACIC production | 0.49–0.52× | 0.43–0.51× | 22.7–58.2× | 54.4–71.2× |
| 4 | ACIC TLS (acic_tls) | 0.61–0.64× | 0.53–0.64× | 27.8–72.7× | 67.7–87.4× |
| 8 | ACIC production | 0.72–0.80× | 0.61–0.78× | — | — |
| 16 | ACIC production | 0.85–1.02× | 0.72–0.99× | 42.9–47.8× | — |
| 16 | ACIC TLS (acic_hint2_tls) | 1.06–1.25× | 0.90–1.22× | 52.0–59.2× | — |

The quarter-size mesh does not cross one-node GAPBS or Wasp on the median
with the production build; the TLS build reaches 1.06–1.25× over GAPBS at 16
nodes. This was the recorded prediction: rounds follow the diameter, which
halves, while shared-memory work follows size, which quarters. Attempts per
edge rise from 1.30 at one node to 3.3 at 16. `mesh24-z` at 4 nodes is the
first point of the mesh weak-scaling series (about 4.2M vertices per node).

#### `mesh26-z` (67.1M vertices, 268.4M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 1 | ACIC production | 1.63–1.78 | 4 | 5536321, 5536322 |
| 1 | GAPBS | 0.316–0.352 | 4 | 5529591, 5538465 |
| 1 | Wasp | 0.262–0.293 | 4 | 5536541 |
| 2 | ACIC production | 0.958–1.05 | 4 | 5536321, 5536322 |
| 4 | ACIC production | 0.554–0.604 | 4 | 5536321, 5536322 |
| 8 | ACIC production | 0.332–0.384 | 4 | 5534022, 5534023, 5536321, 5536322 |
| 16 | ACIC production | 0.223–0.277 | 4 | 5534022, 5534023, 5536321, 5536322, 5541196, 5541240, 5541370 |
| 16 | ACIC TLS (acic_hint2_tls) | 0.181–0.224 | 4 | 5541196, 5541240 |
| 16 | ACIC TLS (acic_tls) | 0.181–0.223 | 4 | 5541370 |
| 16 | Gluon (Async) | 12.3–15.8 | 4 | 5541196, 5541240, 5541370 |
| 16 | RIKEN | 18.9–52.1 | 4 | 5536476, 5541370 |
| 64 | ACIC production | 0.165–0.223 | 4 | 5541660 |
| 64 | ACIC TLS (acic_tls) | 0.138–0.181 | 4 | 5541660 |
| 64 | Gluon (Async) | 8.19–11.4 | 4 | 5541371 |
| 64 | RIKEN | 8.98–20.2 | 4 | 5541371 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 1 | ACIC production | 0.19–0.20× | 0.15–0.17× | — | — |
| 2 | ACIC production | 0.33–0.34× | 0.26–0.28× | — | — |
| 4 | ACIC production | 0.57–0.59× | 0.46–0.49× | — | — |
| 8 | ACIC production | 0.91–0.95× | 0.70–0.82× | — | — |
| 16 | ACIC production | 1.25–1.41× | 0.97–1.22× | 49.3–57.3× | 76.2–169× |
| 16 | ACIC TLS (acic_hint2_tls) | 1.57–1.74× | 1.21–1.50× | 61.1–71.0× | 104–232× |
| 16 | ACIC TLS (acic_tls) | 1.55–1.74× | 1.20–1.50× | 61.4–71.8× | 94.0–206× |
| 64 | ACIC production | 1.56–1.91× | 1.23–1.65× | 44.2–51.0× | 54.4–90.4× |
| 64 | ACIC TLS (acic_tls) | 1.95–2.29× | 1.52–1.98× | 55.4–62.9× | 65.3–113× |

The accepted mesh result: at 16 nodes (896 PEs) production has a 1.25–1.41×
speedup over GAPBS across four allocations, and the same class wins on Anvil
(below). From 1 to 16 nodes production strong-scales 6.6× (41% efficiency,
§8); from 16 to 64 nodes it gains another 1.3×. Gluon selected 8 ranks per
node, oec and Δ 64 in every allocation. RIKEN at 16 nodes: job 5536476 (Δ 1024)
and 5541370.

#### `mesh28-z` (268.4M vertices, 1,073.7M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 1 | GAPBS | 1.51–1.57 | 4 | 5541661 |
| 1 | Wasp | 0.911–1.01 | 4 | 5541662 |
| 16 | ACIC production | 0.742–0.872 | 4 | 5541663 |
| 16 | ACIC TLS (acic_tls) | 0.595–0.691 | 4 | 5541663 |
| 16 | Gluon (Async) | 47.9–61.6 | 2 | 5541663 |
| 16 | RIKEN | 368–489 | 2 | 5541663 |
| 64 | ACIC production | 0.397–0.494 | 4 | 5541664 |
| 64 | ACIC TLS (acic_tls) | 0.327–0.409 | 4 | 5541664 |
| 64 | Gluon (Async) | 26.9–36.2 | 4 | 5541664 |
| 64 | RIKEN | 101–159 | 4 | 5541664 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 16 | ACIC production | 1.74–2.11× | 1.04–1.29× | 61.3–78.1× | 466–625× |
| 16 | ACIC TLS (acic_tls) | 2.19–2.64× | 1.32–1.61× | 75.7–97.1× | 580–772× |
| 64 | ACIC production | 3.07–3.95× | 1.85–2.41× | 59.3–86.3× | 241–322× |
| 64 | ACIC TLS (acic_tls) | 3.70–4.79× | 2.23–2.92× | 71.6–105× | 292–389× |

At 16 nodes Gluon and RIKEN cover two of the four sources: job 5541663 died
on a Lustre I/O error while writing its log after ACIC had finished. Gluon and
RIKEN settings are pinned from the smaller meshes (Gluon 8 ranks, oec, Δ 64;
RIKEN 8 ranks, Δ 1024). From 16 to 64 nodes ACIC gains 1.8×. Weak-scaling
series (about 4.2M vertices per node), production: `mesh24-z`@4 0.143–0.159 s,
`mesh26-z`@16 0.223–0.277 s, `mesh28-z`@64 0.397–0.494 s.

#### `mesh30-z` (1,073.7M vertices, 4,294.8M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 1 | GAPBS | 6.24–6.92 | 4 | 5541661 |
| 1 | Wasp | 3.25–3.37 | 4 | 5541662 |
| 32 | ACIC production | 1.64–1.98 | 4 | 5541665 |
| 32 | ACIC TLS (acic_tls) | 1.32–1.57 | 4 | 5541665 |
| 32 | Gluon (Async) | 141–176 | 4 | 5541665 |
| 64 | ACIC production | 1.09–1.40 | 4 | 5541667 |
| 64 | ACIC TLS (acic_tls) | 0.887–1.12 | 4 | 5541667 |
| 64 | Gluon (Async) | 87.6–171 | 4 | 5541667 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 32 | ACIC production | 3.50–3.80× | 1.65–2.05× | 76.8–107× | — |
| 32 | ACIC TLS (acic_tls) | 4.40–4.73× | 2.07–2.56× | 95.1–134× | — |
| 64 | ACIC production | 4.95–5.73× | 2.32–3.10× | 68.2–157× | — |
| 64 | ACIC TLS (acic_tls) | 6.16–7.04× | 2.90–3.80× | 85.1–192× | — |

Two repetitions per source. RIKEN has no held-out time: at 32 nodes two
launches ran past the 1,590 s limit, and at 64 nodes it took 1,024–1,469 s per
solve on the training sources, roughly 1,000× ACIC's time (different
sources, so not a paired speedup). From 32 to 64 nodes ACIC gains 1.4–1.5×.

### Roads (Morton-ordered by coordinates)

#### `road-usa-z` (23.9M vertices, 57.7M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 1 | GAPBS | 0.116–0.142 | 4 | 5529591 |
| 1 | Wasp | 0.067–0.077 | 4 | 5536541 |
| 4 | ACIC production | 0.214–0.284 | 4 | 5541369 |
| 4 | ACIC TLS (acic_tls) | 0.177–0.233 | 4 | 5541369 |
| 4 | Gluon (Async) | 6.27–23.0 | 4 | 5541369 |
| 8 | ACIC production | 0.162–0.226 | 4 | 5538405, 5538406 |
| 16 | ACIC production | 0.141–0.196 | 4 | 5538405, 5538406, 5541195, 5541196, 5541370 |
| 16 | ACIC TLS (acic_hint2_tls) | 0.118–0.165 | 4 | 5541195, 5541196 |
| 16 | ACIC TLS (acic_tls) | 0.118–0.165 | 4 | 5541370 |
| 16 | Gluon (Async) | 3.91–8.74 | 4 | 5541195, 5541196, 5541370 |
| 64 | ACIC production | 0.113–0.162 | 4 | 5541660 |
| 64 | ACIC TLS (acic_tls) | 0.100–0.147 | 4 | 5541660 |
| 64 | Gluon (Async) | 2.59–5.29 | 4 | 5541371 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 4 | ACIC production | 0.50–0.55× | 0.27–0.33× | 27.9–103× | — |
| 4 | ACIC TLS (acic_tls) | 0.61–0.67× | 0.33–0.40× | 34.0–123× | — |
| 8 | ACIC production | 0.63–0.73× | 0.34–0.43× | — | — |
| 16 | ACIC production | 0.72–0.84× | 0.39–0.50× | 26.2–60.0× | — |
| 16 | ACIC TLS (acic_hint2_tls) | 0.86–1.00× | 0.47–0.59× | 31.2–73.0× | — |
| 16 | ACIC TLS (acic_tls) | 0.86–1.00× | 0.47–0.60× | 31.7–72.1× | — |
| 64 | ACIC production | 0.88–1.05× | 0.48–0.62× | 21.5–46.5× | — |
| 64 | ACIC TLS (acic_tls) | 0.97–1.18× | 0.53–0.70× | 24.1–52.4× | — |

RIKEN is out of range (distances past 2^24). Training-source cells not in the
table: at 8 Frontier nodes production had a 0.66–0.69× speedup over GAPBS
(jobs 5534016/5534017). Width 131072 has a 1.45–2.15× speedup over the plain
mesh candidate, which does 10.8–11.0 attempts per edge against 3.1. Gluon's Δ
surface is flat (3.9–4.6 s on training sources). ACIC's time falls only 1.2×
from 16 to 64 nodes: road is bound by round latency (§11).

#### `road-na-z` (62.1M vertices, 155.0M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 1 | GAPBS | 0.672–0.685 | 4 | 5541358 |
| 1 | Wasp | 0.288–0.318 | 4 | 5541359 |
| 4 | ACIC production | 0.601–0.713 | 4 | 5541426 |
| 4 | ACIC TLS (acic_tls) | 0.487–0.585 | 4 | 5541426 |
| 4 | RIKEN, inexact | 143–629 | 4 | 5541713 |
| 16 | ACIC production | 0.390–0.486 | 4 | 5541428 |
| 16 | ACIC TLS (acic_tls) | 0.315–0.405 | 4 | 5541428 |
| 16 | Gluon (Async) | 11.1–30.7 | 4 | 5541428 |
| 16 | RIKEN, inexact | 51.2–201 | 4 | 5541715 |
| 64 | ACIC production | 0.335–0.457 | 4 | 5541660 |
| 64 | ACIC TLS (acic_tls) | 0.279–0.384 | 4 | 5541660 |
| 64 | Gluon (Async) | 5.52–12.9 | 4 | 5541429 |
| 64 | RIKEN, inexact | 22.9–86.9 | 4 | 5541717 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 4 | ACIC production | 0.95–1.14× | 0.41–0.53× | — | 238–889× |
| 4 | ACIC TLS (acic_tls) | 1.15–1.41× | 0.49–0.65× | — | 294–1076× |
| 16 | ACIC production | 1.38–1.76× | 0.59–0.82× | 22.8–78.8× | 131–414× |
| 16 | ACIC TLS (acic_tls) | 1.66–2.17× | 0.71–1.01× | 27.4–97.5× | 163–496× |
| 64 | ACIC production | 1.49–2.05× | 0.63–0.95× | 12.5–38.4× | 68.3–197× |
| 64 | ACIC TLS (acic_tls) | 1.77–2.45× | 0.75–1.14× | 15.1–46.1× | 82.0–239× |

RIKEN on `road-na-z` reaches every vertex but its distance sum is high by 3.0e-05–3.7e-05 (relative).

No Gluon at 4 nodes: the first attempt (5541426) was cancelled after its
ACIC arms, because 60–85 s Gluon solves left no time for the search, and its
rerun (5541712) failed at submission (missing `VARIANTS_PREFIX`). RIKEN ran in
separate jobs; 5541713 died on a Lustre I/O error after 6 of its 8 held-out
solves, so each source has one or two.

#### `road-eu-z` (95.6M vertices, 228.7M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 1 | GAPBS | 0.632–0.693 | 4 | 5541358 |
| 1 | Wasp | 0.357–0.377 | 4 | 5541359 |
| 4 | ACIC production | 0.711–0.853 | 4 | 5541426 |
| 4 | ACIC TLS (acic_tls) | 0.590–0.708 | 4 | 5541426 |
| 4 | RIKEN, inexact | 255–1542 | 2 | 5541714 |
| 16 | ACIC production | 0.373–0.509 | 4 | 5541428 |
| 16 | ACIC TLS (acic_tls) | 0.305–0.413 | 4 | 5541428 |
| 16 | Gluon (Async) | 7.40–41.1 | 4 | 5541428 |
| 16 | RIKEN, inexact | 87.2–491 | 4 | 5541716 |
| 64 | ACIC production | 0.317–0.442 | 4 | 5541660 |
| 64 | ACIC TLS (acic_tls) | 0.265–0.364 | 4 | 5541660 |
| 64 | Gluon (Async) | 3.64–16.1 | 4 | 5541429 |
| 64 | RIKEN, inexact | 33.3–196 | 4 | 5541717 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 4 | ACIC production | 0.81–0.89× | 0.42–0.53× | — | 358–1808× |
| 4 | ACIC TLS (acic_tls) | 0.98–1.07× | 0.50–0.64× | — | 432–2179× |
| 16 | ACIC production | 1.36–1.69× | 0.70–1.01× | 14.5–110× | 234–965× |
| 16 | ACIC TLS (acic_tls) | 1.68–2.07× | 0.86–1.24× | 17.9–135× | 286–1187× |
| 64 | ACIC production | 1.57–1.99× | 0.81–1.19× | 8.24–50.8× | 105–442× |
| 64 | ACIC TLS (acic_tls) | 1.90–2.39× | 0.98–1.43× | 10.0–60.9× | 126–537× |

RIKEN on `road-eu-z` reaches every vertex but its distance sum is high by 4.9e-05–6.4e-05 (relative).

No Gluon at 4 nodes (as `road-na-z`). RIKEN at 4 nodes ran two sources once
each (up to 1,542 s per solve).

### Scale-free

#### `orkut` (3.1M vertices, 234.4M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 16 | ACIC production | 0.065–0.067 | 4 | 5538390, 5538392, 5539286, 5539985 |
| 16 | ACIC TLS (acic_hint2_tls) | 0.062 | 4 | 5539286, 5539985 |
| 16 | Gluon (Async, Sync) | 0.940–1.04 | 4 | 5539286, 5539985 |
| 16 | RIKEN | 0.029–0.032 | 4 | 5536474, 5536475 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 16 | ACIC production | — | — | 14.2–16.2× | 0.46–0.47× |
| 16 | ACIC TLS (acic_hint2_tls) | — | — | 15.0–16.9× | 0.47–0.51× |

Gluon relaxes each edge 7.5–12.7 times and spends 78–85% of its time in its
sync phase. RIKEN is about 2× faster than ACIC.

#### `uniform25` (33.6M vertices, 1,073.7M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 16 | ACIC production, 4 × 14 | 0.224–0.238 | 4 | 5538389, 5538391, 5539286, 5539985 |
| 16 | ACIC TLS (acic_hint2_tls), 4 × 14 | 0.210–0.232 | 4 | 5539286, 5539985 |
| 16 | Gluon (Async, Sync) | 2.14–2.35 | 4 | 5539286, 5539985 |
| 16 | RIKEN | 0.181–0.189 | 4 | 5536474, 5536475 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 16 | ACIC production, 4 × 14 | — | — | 8.88–10.8× | 0.77–0.84× |
| 16 | ACIC TLS (acic_hint2_tls), 4 × 14 | — | — | 9.09–11.2× | 0.81–0.90× |

The closest scale-free graph to RIKEN (0.77–0.90×).

#### `rmat25` (33.6M vertices, 1,047.2M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 16 | ACIC production, 4 × 14 | 0.203–0.212 | 4 | 5538389, 5538391, 5539286, 5539985 |
| 16 | ACIC TLS (acic_hint2_tls), 4 × 14 | 0.157–0.168 | 4 | 5539286, 5539985 |
| 16 | Gluon (Async, Sync) | 0.782–0.849 | 4 | 5539286, 5539985 |
| 16 | RIKEN | 0.062–0.068 | 4 | 5536474, 5536475 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 16 | ACIC production, 4 × 14 | — | — | 3.75–4.16× | 0.29–0.33× |
| 16 | ACIC TLS (acic_hint2_tls), 4 × 14 | — | — | 4.58–5.27× | 0.38–0.43× |

The hub hints (TLS builds) cut `rmat26`'s updates from 1.81B to 0.68B (§11);
ACIC stays about 2.2–3× slower than RIKEN on RMAT.

#### `rmat26` (67.1M vertices, 2,103.8M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 16 | ACIC production, 4 × 14 | 0.358–0.386 | 4 | 5538389, 5538391, 5539899, 5539985 |
| 16 | ACIC TLS (acic_hint2_tls), 4 × 14 | 0.271–0.307 | 4 | 5539899 |
| 16 | Gluon (Async, Sync) | 0.966–1.19 | 4 | 5539899, 5539985 |
| 16 | RIKEN | 0.122–0.124 | 4 | 5536474, 5536475 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 16 | ACIC production, 4 × 14 | — | — | 2.48–3.49× | 0.32–0.35× |
| 16 | ACIC TLS (acic_hint2_tls), 4 × 14 | — | — | 3.17–4.44× | 0.40–0.46× |

The TLS row is allocation A only (5539899). Source 27797227 stalled twice,
in both builds, answering correctly after 0.2–1.8 s (§13); allocation B reran
production alone after the stall.

#### `rmat27` (134.2M vertices, 4,223.3M edges)

| Nodes | Implementation | Time per solve (s) | Sources | Jobs |
|---:|---|---:|---:|---|
| 16 | ACIC production, 4 × 14 | 0.714–0.810 | 4 | 5538389, 5538391, 5539900, 5539985 |
| 16 | ACIC TLS (acic_hint2_tls), 4 × 14 | 0.557–0.576 | 4 | 5539900, 5539985 |
| 16 | Gluon (Async, Sync) | 1.27–1.46 | 4 | 5539900, 5539985 |
| 16 | RIKEN | 0.230–0.242 | 4 | 5536474, 5536475 |

ACIC's speedup (baseline time / ACIC time, range over held-out sources; GAPBS and Wasp on one node):

| Nodes | ACIC build | over GAPBS | over Wasp | over Gluon | over RIKEN |
|---:|---|---:|---:|---:|---:|
| 16 | ACIC production, 4 × 14 | — | — | 1.47–2.08× | 0.29–0.33× |
| 16 | ACIC TLS (acic_hint2_tls), 4 × 14 | — | — | 2.22–2.70× | 0.40–0.43× |

Gluon's cost grows more slowly with RMAT scale than ACIC's, so ACIC's
speedup over Gluon falls from about 4× on `rmat25` to about 2× here.

### Anvil (8 nodes, not regenerated here)

| Dataset | Nodes | ACIC build | Result | Jobs |
|---|---:|---|---|---|
| `mesh26-z` | 8 | 16 processes × 7 workers, production candidate | speedup over GAPBS 1.21–1.41×, four held-out sources, two allocations | 20866513/20866514; GAPBS 20866515/20866516 |
| `road-usa-z` | 8 | best fixed width/cap arms | speedup over GAPBS about 0.67× (training sources) | 20868020–22, 20876828–30 |
| scale-free suite | 2–8 | revision `de0ed1c` | speedup over RIKEN 0.27–0.53× | 8g comparison |

### Pending

| Dataset | Size | Status |
|---|---|---|
| `road-planet-z` (OSM planet 2025-12-29, largest component) | 199.5M vertices, 478.9M one-way edges before symmetrizing | extracted (job 5545069); files and references in job 5545070 |
| `terrain-ae-z` (Copernicus GLO-90, land south of 50°N, component containing Central Asia) | expected about 8e9 vertices, 1.1 TB | building (job 5545904); references by certified ACIC (`terrain_reference.sbatch`); Gluon via `gluon64.patch` (oec only; validated in job 5545185) |

## Mechanism chain established September 18–23

### 1. Scale-free work growth was repaired

Lazy heavy-edge relaxation, an htram hold bitmap, revised idle flushing and
empty-delivery suppression changed the scale-free trend. ACIC gained a 1.1–2.3×
speedup from two to eight nodes on `rmat25`, Orkut, `rmat26` and `rmat27`.
RIKEN retained a substantial lead, so these changes are a regression defense
and scaling repair rather than a winning scale-free result.

### 2. Sparse one-node execution was reorganized

Reader tiling and process-shared state exposed more useful parallelism on mesh
and road. Work-cost instrumentation then showed that redundant edge attempts
and queue operations, rather than one unnamed Charm++ overhead, dominated the
remaining distributed cost.

Process-wide nearest priority sharply reduced attempts. Removing one entry at
a time was expensive, while batches of eight retained the ordering benefit and
produced large local gains. The frozen batch-8 source is `48ca9c0`; its Anvil
production and diagnostic hashes begin `bd07ed61` and `6daa7dba`.

### 3. Global concurrency was isolated

The fixed-total experiment changed physical placement, priority-domain count
and workers/process separately. Only workers/process crossed the noise rule:
raising 7 to 15 increased road work 1.59–1.84×. Placement and domain count were
within noise. The two performance allocations were jobs 20833718/20833719;
the 224-solve gate was 20833717. Machine-readable audits are
`design/onenode-data/r1-attrib-anvil-20833718.json` and its matching files.

### 4. A runtime scheduler regression was found

Reconverse commit `146ec42` registered queues with a new scheduler. The changed
polling/order adds 30–45% road work, and the registered scheduler's speedup
over `+old-scheduler` is about 0.69–0.77× on road. On the same current runtime,
`+old-scheduler` matches the earlier v0916 behavior within the control floor.
Jobs 20841653/20841654 contain the matched comparison. Every current
performance run therefore uses `+old-scheduler`.

### 5. Heap cadence produces the mesh win

A fixed process drain cap controls road speculation but hurts mesh. Heap slice
8 instead yields to the scheduler after eight removals, allowing messages and
other tasks to interleave. It reduces distributed mesh rework enough to beat
GAPBS at 896 PEs on both machines. On one Frontier node, where cross-process
rework is small, the slice's speedup is 0.93–0.95×; activation is graph/scale
dependent.

### 6. Road becomes round bound after ordering

The default 2,048-bucket histogram covers only about 35K distance units on
road, while observed distances reach 37–54M. The frontier therefore sits in
overflow and supplies almost no global ordering. A fixed wider bucket makes the
range representable and reduces speculation.

The smallest useful width creates too many global rounds. Width 128K is the
best measured compromise, but still loses to GAPBS. Node-level control does not
shorten the solve. On Anvil, early idle-round time grows roughly linearly with
PEs while its runtime has `SPANTREE=0`; the matched spanning-tree screen is
prepared and remains the only open road-runtime test.

### 7. Frontier reproduces mesh and exposes launch bimodality

Every input regenerates byte-identically on Frontier. The mesh result
reproduces, but RMAT and uniform launches randomly enter a slow mode whose
speedup relative to the fast mode is about 0.84× (rmat27, 8 nodes). A whole launch moves together; work counts remain the same, reductions
stay short and remote updates trickle through the tail. Frozen R0 shows the
same behavior, so it is not caused by the recent candidate policies.

Send caps, packet/rendezvous behavior, receive matching, LCI device count,
ASLR, huge pages, NUMA placement/prebinding, fabric congestion and adaptive
solver state have been ruled out. Jobs 5535017–5535803 cover those probes. The
remaining bounded hypothesis is Reconverse backend progress/polling.

### 8. The fixed mesh candidate strong-scales, and each mechanism is causal

On Frontier the unchanged candidate (nearest, batch 8, slice 8, 8 × 7 per
node, `+old-scheduler`) ran `mesh26-z` at 1, 2, 4, 8 and 16 nodes on the four
held-out sources, in two allocations (jobs 5536321/5536322). All 448 timed and
160 work-cost solves passed the audit; allocations agree within 1.5%.

Times and speedups over the baselines are in the `mesh26-z` table above; the
work measurements (medians over sources and allocations):

| Nodes | PEs | Speedup over 1 node | Efficiency | Attempts/edge | Rounds |
|---:|---:|---:|---:|---:|---:|
| 1 | 56 | 1.00× | 100% | 1.35 | 598–607 |
| 2 | 112 | 1.71–1.72× | 85–86% | 1.54 | 612–630 |
| 4 | 224 | 2.91–2.97× | 73–74% | 1.72–1.73 | 550–555 |
| 8 | 448 | 4.68–4.70× | 59% | 1.94–1.95 | 586 |
| 16 | 896 | 6.55–6.58× | 41% | 2.76–2.80 | 500 |

Time falls at every doubling on every source. The loss of efficiency is
extra work, not communication: attempts per edge double from one to sixteen
nodes, updates crossing nodes stay below 0.5% of attempts, and the work-cost
build's idle share rises from 5% to 20%. Queue operations stay about 48% of
work time at every scale. ACIC passes one-node GAPBS between 8 and 16 nodes.

The cumulative ablation at 16 nodes, same allocations and sources:

| Arm | Candidate's speedup over this arm (median) | Attempts/edge | Rounds |
|---|---:|---:|---:|
| Local queue, no batch, no slice | 3.21–3.33× | 18.1–18.2 | 287–288 |
| Nearest, unbatched | 1.83× | 4.00–4.06 | 254–255 |
| Nearest, batch 8 | 1.47–1.56× | 5.09–5.41 | 250–252 |
| Nearest, batch 8, slice 8 (candidate) | 1.00× | 2.76–2.80 | 500 |

Process-wide priority removes 78% of the local queue's work. Batching then
trades 25–35% more work for a 1.15–1.24× speedup over the unbatched arm. The slice halves the remaining
work while doubling the rounds, and is the step that crosses GAPBS. Every
step is faster than the one before on every source in both allocations. 18 of
20 recorded predictions were met in allocation A and 20 of 20 in B; the two
misses were a 2.5% control difference on one source and batch-8 work of 5.41
against a predicted ceiling of 5.4 attempts per edge.

### 9. Wasp and RIKEN references on Frontier

**Wasp** (SC25 artifact, `bin/wasp_sssp`, digest adapter
`benchmarks/wasp_driver.cpp`) matches the independent reference on all 58
sources of seven graphs (job 5536535), so it reads the native weights. Tuned
like GAPBS, a joint thread × Δ search on training sources then four held-out
sources (job 5536541), it selects 56 threads with GAPBS's Δ (4096 on mesh,
32768 on road). Both choices sit at the 56-thread boundary.

Its held-out times and ACIC's speedups over it are in the dataset tables.
Five Wasp launches at mesh Δ 4 hit the 180 s launch limit (the other four at
that Δ took 52–68 s, against GAPBS's 4–7 s); they are recorded and do not
affect the selection.

**RIKEN** at 16 nodes (jobs 5536474/5536475, independent searches) selects
8 ranks/node with Δ 16 (rmat25/26/27) or 64 (orkut, uniform25) in both
allocations; held-out medians agree within 1%. On `mesh26-z` (job 5536476,
one allocation) it selects Δ 1024. `road-usa-z` is outside RIKEN's
exact-distance range, and on the OSM roads it is inexact (dataset notes). The
only failures are the known Δ-equal-to-denominator aborts.

**ACIC on the scale-free graphs at 16 nodes** chose 4 × 14 for rmat25,
uniform25, rmat26 and rmat27 and 8 × 7 for orkut on training sources (job
5536460). The launch bimodality nearly disappears at 16 nodes: rmat25, rmat27,
uniform25 and orkut ran every launch in one mode, and rmat26 had 1–2 slow
launches of 8 at negligible cost.

Held-out confirmation, 16 nodes, four held-out sources, 8 launches per arm, two
allocations (jobs 5538389–5538392; 720 audited solves), paired with RIKEN's
allocation of the same letter; the times are the production rows of the
scale-free dataset tables. One of 16 orkut launches was slow in allocation A;
no other launch was.

ACIC is slower than RIKEN on every source of every scale-free graph: a speedup
of 0.77–0.85× on `uniform25` and 0.28–0.48× elsewhere. Every recorded
prediction (written as time ratios before the reporting rule changed) was met.
With almost no slow launches, fast-mode and plain medians agree within 0.1%.
The repeated-control prediction (within 3%) held for orkut but missed on all
four 4 × 14 graphs in both allocations: at 4 × 14, repeated launches on the
same source spread about ±6% in both directions (control/candidate
0.95–1.06), a continuous spread rather than two modes. That floor is too wide
for a few-percent regression decision at this layout, but not for these
comparisons.

**Second mesh size** (`mesh24-z`, jobs 5538412/5538413, times in the dataset
table): the 16-node repeat was within 2% except one source in allocation B
(3.5%), and the no-crossing result was the recorded prediction. The mesh win
therefore needs enough work per round; `mesh26-z` and larger cross, the
quarter-size mesh does not.

**Road on held-out sources** (jobs 5538405/5538406, times in the `road-usa-z`
table): repeats within 2%. Both comparison predictions (recorded as time
ratios, GAPBS 1.3–1.8 and Wasp 2.2–3.2) missed on the favorable side; the
node-count, width and control predictions were met.

### 10. Frontier gates: RMAT regression NO-GO by the recorded rule; spanning tree kept, small effect

**RMAT regression gate** (jobs 5538463/5538464, 16 nodes, 8 × 7, four held-out
sources, 16 launches per arm; 2,040 audited solves). The recorded pass rule is
`onenode_accept.py --nodes 16 --variant n16_s8 --one-node-variant n1_s8
--regression-reps 16` over allocations (5536321, GAPBS 5529591, 5536321,
5538463) and (5536322, GAPBS 5538465, 5536322, 5538464). Result: **NO-GO.**

A graph passes if the candidate's worst per-source speedup over frozen R0 is
no lower than the noise bound set by the repeated R0 (1 / the largest
control-versus-frozen difference).

| Graph | Candidate's worst per-source speedup over frozen R0 (A / B) | Noise bound (A / B) | Verdict |
|---|---:|---:|---|
| `mesh26-z` C6 | speedup over GAPBS 1.29× / 1.28× median, worst 1.25× / 1.27× | — | pass, both |
| `rmat25` | 0.972 / 0.982 | 0.942 / 0.944 | pass, both |
| orkut | 0.990 / 0.975 | 0.978 / 0.972 | pass, both |
| `uniform25` | 0.945 / 0.999 | 0.919 / 0.948 | pass, both |
| `rmat26` | 0.949 / 0.965 | 0.967 / 0.982 | **fail, both** |
| `rmat27` | 0.975 / 0.955 | 0.976 / 0.976 | **fail, both** |

What the failures are made of:

- **`rmat26`**: each allocation fails on one source, and a different one each
  time (speedup 0.950 on source 1 in A, 0.965 on source 4 in B); the other
  sources are 0.99–1.03×. No launch was slow. On fast-mode medians the
  candidate's speedup over frozen is 0.974× in A and 1.022× in B.
- **`rmat27`**: the slow launch mode returned at 16 nodes on this graph, and
  more often for the candidate (7/16 and 4/16) than for frozen (4/16, 0/16) or
  control (2/16, 0/16); pooled, 11/32 against 6/64 (Fisher p = 0.004). In fast
  mode the candidate's speedup over frozen is 1.008× and 1.006×, so its fast
  solves did not regress. The opposite imbalance appeared on `uniform25` in B (candidate 0/32
  against R0 11/64 pooled over allocations, p = 0.014), so a mode-rate
  difference is not yet shown to be a property of the candidate.
- Predictions: every control-versus-frozen difference was under 10% (met); every graph passes (missed
  on `rmat26`, `rmat27`); at most 2 slow launches of 48 per graph (missed on
  `rmat27` in A, 13/48, and `uniform25` in B, 11/48).

The failures are speedups of 0.95–0.975× on single sources, at the resolution limit the plan
stated for this gate. The recorded rule decides this run; a mode-aware rule
(fast-mode ratio within the floor and no higher slow rate) or more allocations
would have to be recorded before a new run, not applied to this one.

**Spanning-tree screen** (jobs 5538468/5538469, road, 8 nodes, training
sources; tree = `acic_slice` on the campaign runtime, flat = `acic_flat` on the
same Reconverse/LCI built with `SPANTREE=OFF`). The tree lowers round cost on
every arm in both allocations, but by 0.002–0.027 ms, not the predicted
≥ 0.05 ms. The tree's speedup over flat is 1.09–1.11× on `w32k` (predicted at
least 20% faster), 1.005–1.04× on `w128k` (at least 10%) and 0.99–1.01× on
`4x14_cap7` (at least 10%). Work stays within 10%,
repeats within 2%, and `w64k` within 10% of `w128k`. By the plan's rule
`SPANTREE=ON` stays the campaign setting, and the screen stops here: the
broadcast tree is not what makes road's rounds cost 0.16–0.31 ms. The
remaining per-round cost is in the reduction or Main's per-round work.

### 11. RMAT profile: the waste is updates to hubs that are already final

A PC-sampling profile, a Projections trace and RIKEN's relaxation counter on
`rmat26` at 16 Frontier nodes, 4 × 14 (jobs 5538732, 5538734, 5538765), give
a work-versus-cost split. RIKEN sends 1.22B relaxations in 0.125 s. ACIC
creates 1.81B updates plus 0.19B lazy tokens in 0.357 s: about 1.5× the work
at about 1.9× the cost per relaxation. 89% of ACIC's arrivals (1.61B) find
their target already final, and 1.33B of those land on vertices of degree
≥ 128. The samples have no single hotspot. Two exact micro-fixes aimed at the
largest entries (a lazy-range boundary table and a vector hold FIFO) are
within the 0.94–1.11× control spread at 16 nodes (jobs 5538752/5538753).

Hub-distance hints target the waste directly. Vertices of degree ≥ 256
publish their current distance once per controller round to a per-process
table, and senders drop updates no better than it. This is exact because
distances only fall; it needs no settled test. With the auto gate (the
lazy-heavy regime), at 16 nodes over two allocations (jobs 5538953/5538954),
the speedup over frozen is 1.25–1.26× on `rmat25`, 1.30–1.34× on `rmat26`,
1.23–1.30× on `rmat27` and 1.01–1.07× on orkut; `uniform25` is unchanged.
`rmat26` updates fall from 1.81B to 0.68B, under RIKEN's count, yet ACIC stays
about 2.2× slower than RIKEN. The remaining cost is the sender's edge scan
and probe, heap and tokens, runtime polling and an idle tail, not update
volume. Adding the initial-exec TLS runtime gives a further 1.00–1.07×.
These are training-source results; held-out confirmation is pending.

The same instruments on `road-usa-z` at 16 nodes (job 5538868) show PEs idle
61.5% of the solve, with about 800 controller rounds of about 0.23 ms each.
Road at this size is bound by round latency, not work, which is why larger
road inputs are the next test (`road-eu`, `road-na`; `scripts/frontier/prepare_osm.sbatch`).

### 12. 64-bit vertex ids and a distributed certificate

The compact htram wire (8 bytes, step 7.6j) held vertex ids below 2^31 and
distances below 2^32. `make WIRE=compact64` builds a 12-byte wire (47-bit
vertex, 48-bit distance, overflow flag; `weighted_node_struct.h`), and the
GAPBS reader accepts the 64-bit-id layout a GAPBS built with `int64_t` NodeID
writes (`graph_convert gen ... --wide` writes it). A graph too large for serial
Dijkstra proves its own answer with `--certify`: every PE sends d(u) + w along
every edge; no candidate below d(v), a tight edge into every reached vertex but
the source, d(source) = 0 and positive weights make the distances exact.
Also fixed: the send filter treated vertex 0xffffffff as its empty entry (a
wrong answer past 2^32 vertices), and the owner-table divisor now grows with V.

| Test | Result |
|---|---|
| Wire round trips, both widths (`tests/test_wire.cpp`) | exact at every field boundary |
| 2 nodes, road-ny, youtube, mesh22, rmat22, two held-out sources, `acic_w32` and `acic_w64` (job 5540134) | reference digest and `CERTIFY PASS` on all 16 solves |
| Same generated mesh and RMAT with 32- and 64-bit id files | identical digests; serial `--verify` passes |
| `ACIC_CERTIFY_PERTURB` (one distance + 1) | `CERTIFY FAIL`, exit 1 |
| In-memory mesh, V = 2^32 + 2^30 (73271^2 reachable, 21.5B edges, 430 GB of graph), sources 5,000,000,000 and 17, 8 and 16 nodes, 8 x 7 (job 5540170) | `CERTIFY PASS` on all four solves; digests identical at 8 and 16 nodes; `acic_w32` refuses the graph |

The certificate is cheap: 0.26 s for 21.5B edges at 16 nodes. The solve is not
tuned at this size: 88 s (source 5e9) and 54 s (source 17) at 16 nodes, a
1.5x speedup over 8 nodes, with the generated mesh's row-major strip
partition and the default width rule. The 12-byte wire's cost on graphs that
fit the 8-byte one is not yet measured.

### 13. Gluon: how it was run and why it loses

Tuned Gluon (D-Galois sssp-push) ran in the same allocation as ACIC on the
held-out sources: at 16 nodes on the scale-free graphs (A: jobs 5539286,
5539899, 5539900; B: 5539985; 215 Gluon runs in each, every one valid; Async
and Sync each tuned over 1 or 8 ranks per node, oec/cvc and Δ), at 16 nodes on
`mesh24-z`, `mesh26-z` and `road-usa-z` (A: 5541240, 5541195; B: 5541196;
Async only), and in the larger-input study at 4–64 nodes (5541369 onward;
`design/large-scale-plan.md`). Results are in the dataset tables;
`benchmarks/gluon_speedup.py` gives the per-job pairing.

Gluon spends 78–85% of its time in its sync phase and relaxes each edge
7.5–12.7 times on orkut. Gluon-Sync was 20–40× slower than Async in every mesh
tuning run (100–260 s per `mesh24-z`/`mesh26-z` solve, 80% in its sync phase;
three Sync candidates passed the 330 s launch cap), so mesh and road use Async
only. "Async is the faster Gluon mode everywhere" missed: Sync won on `rmat25`,
`rmat27` and `uniform25`. The TLS builds' gain on meshes and roads is not hub
hints (auto resolves off: 0 published, same round counts) but cheaper rounds,
most likely the initial-exec TLS runtime; the recorded prediction (0.97–1.05×)
missed.

`rmat26` source 27797227 stalled twice (hints build, job 5539287; production
`acic_slice`, job 5539985), answering correctly after 0.2–1.8 s with ~25–59K
updates counted live but almost none held; the fallback reran production
alone. It is a pre-existing liveness defect, not a hints one, and the
192-solve probe (job 5539909) did not reproduce it.

### 14. Past 2^32 vertices: Gluon patched, ACIC reading wide files

`benchmarks/gluon64.patch` makes Gluon's oec path 64-bit clean: it fixes
`retrieveMaster`, `G2LEdgeCut`'s offset, edge inspection and `fillMirrors`,
which truncated global ids to 32 bits, and teaches `BufferedGraph` version-2
(64-bit) `.gr` files. Other partitions and a forced gids data mode refuse graphs
past 2^32. Job 5545185 (4 nodes): stock and patched Gluon return identical,
reference-equal digests on `mesh24-z` and `road-usa-z` from version-1 and
version-2 files at the same speed (4.7 against 4.8 s; 15.5 against 15.3 s).
On `mesh20` with ids multiplied by 4,200 (4.4e9 ids, one source above 2^32),
ACIC's certificate passes, its reachable count, distance sum and maximum
distance equal scipy's Dijkstra on `mesh20`, and patched Gluon's full digest
equals ACIC's; patched Gluon refuses cvc on that graph.

## Evidence and provenance

| Evidence | Machine-readable record / configuration |
|---|---|
| Anvil mesh C6 | `design/onenode-data/c6-mesh-8n-anvil-20866513.json` and matching 20866514 record; `benchmarks/c6-mesh-8n-variants.json` |
| Frontier mesh C6 | Frontier summaries under `design/onenode-data/` with job IDs 5534022/5534023; `benchmarks/frontier-c6-mesh-variants.json` |
| Anvil road ordering/rounds | `design/onenode-data/road-order-8n-anvil-20868022.json`, road-round records 20876828–30; `benchmarks/road-order-8n-variants.json`, `benchmarks/road-rounds-8n-variants.json` |
| Frontier road | `design/onenode-data/frontier-road-rounds-5534016.json` and 5534017; `benchmarks/frontier-road-rounds-8n-variants.json` |
| Runtime scheduler | layout and old-scheduler JSON summaries under `design/onenode-data/`; `benchmarks/oldsched-variants.json` |
| Frontier mesh strong scaling and ablation | `design/onenode-data/frontier-mesh-scaling-5536321.json`, `-5536322.json` and `frontier-mesh-scaling-report.json` (`benchmarks/mesh_scaling_report.py`); `benchmarks/frontier-mesh-scaling-variants.json`, predictions recorded in commit `026e502` |
| Frontier Wasp and RIKEN references | `design/onenode-data/frontier-wasp-1n-5536541.json`, `frontier-riken-16n-5536474.json`, `-5536475.json`, `-5536476.json`; smoke job 5536535 |
| Frontier scale-free layout selection | `design/onenode-data/frontier-scalefree-layout-5536460.json` and `frontier-scalefree-layout-modes-5536460.json`; `benchmarks/frontier-scalefree-layout-variants.json` |
| Frontier scale-free held-out vs RIKEN | `design/onenode-data/frontier-scalefree-heldout-{5538389,5538390,5538391,5538392}.json`, matching `-modes-` files, `frontier-scalefree-vs-riken-16n.json`; `benchmarks/frontier-scalefree-heldout-{4x14,8x7}-variants.json` |
| Frontier road held-out | `design/onenode-data/frontier-road-heldout-5538405.json`, `-5538406.json`; `benchmarks/frontier-road-heldout-variants.json`, predictions in commit `8749a7d` |
| Frontier mesh24-z | `design/onenode-data/frontier-mesh24-scaling-5538412.json`, `-5538413.json`, `frontier-gap-mesh24-5538410.json`, `frontier-wasp-mesh24-5538411.json`; `benchmarks/frontier-mesh24-scaling-variants.json` |
| Frontier RMAT gate | `design/onenode-data/frontier-rmat-gate-16n-5538463.json`, `-5538464.json`, `-modes-` files, `frontier-accept-16n.json`; `benchmarks/frontier-rmat-gate-16n-variants.json`; GAPBS second allocation 5538465 |
| Frontier spanning-tree screen | `design/onenode-data/frontier-road-spantree-5538468.json`, `-5538469.json`; `benchmarks/frontier-road-spantree-8n-variants.json` |
| Every held-out Frontier result by dataset | `design/onenode-data/results-by-dataset.json` (`benchmarks/results_by_dataset.py`) |
| Frontier Gluon, 16 nodes | `design/onenode-data/frontier-gluon-scalefree-16n.json`, `frontier-gluon-meshroad-16n.json` (`benchmarks/gluon_speedup.py`); `benchmarks/frontier-gluon-*-16n-variants.json` |
| Frontier larger-input study, allocation A | `design/onenode-data/frontier-large-A-summary.txt` (`benchmarks/large_summary.py`); `design/large-scale-plan.md`; `benchmarks/frontier-large-*-variants.json` |
| Gluon past 2^32 | job 5545185 (`scripts/frontier/gluon64_test.sbatch`), `benchmarks/gluon64.patch` |
| Frontier RMAT behavior | `design/onenode-data/frontier-rmat-regression-5534333.json` and 5534334 plus probe configurations named `benchmarks/frontier-*-probe-variants.json` |

Anvil raw logs are rooted at `/anvil/scratch/x-rrao/acic/`; Frontier raw logs
at `/lustre/orion/csc710/scratch/rrao/acic/campaign/logs/`. Build identities
and paths are consolidated in [configurations.md](configurations.md).

## Claims supported now

1. At 896 CPU PEs, sliced asynchronous ACIC beats tuned one-node GAPBS on the
   measured `mesh26-z` class on two machines and held-out sources. Against
   tuned Wasp on Frontier its median speedup is 1.08×, but not every source is
   above 1×.
2. Process-wide priority plus batched removal reduces redundant sparse-graph
   work, and heap slicing converts that reduction to a distributed mesh gain.
   At 16 Frontier nodes each step of the cumulative ablation is faster than the
   previous one; the candidate's speedup over the local-queue arm is 3.2–3.3×.
3. On road, a representable global ordering window approaches minimal edge
   work, after which controller round cost is the dominant measured limit.
4. Runtime scheduling materially changes asynchronous SSSP work; registered
   scheduling causes a reproduced regression on road (a speedup of about
   0.69–0.77× relative to `+old-scheduler`).
5. The fixed mesh candidate strong-scales from 1 to 16 Frontier nodes at
   6.6× (41% efficiency), with time falling at every doubling; the efficiency
   loss is measured redundant work, not inter-node traffic. Beyond 16 nodes it
   keeps gaining on meshes (`mesh26-z` 1.3× from 16 to 64, `mesh28-z` 1.8×,
   `mesh30-z` 1.4–1.5× from 32 to 64) but barely on roads.
6. ACIC is faster than tuned Gluon on every graph, node count and held-out
   source measured (1.5–192×), and faster than RIKEN on every mesh and road.
7. On meshes from `mesh26-z` up and on the OSM roads, ACIC at 16–64 nodes beats
   tuned one-node GAPBS, and the mesh margin over GAPBS and Wasp grows with
   input size (`mesh30-z` at 64 nodes: 5.0–7.0× and 2.3–3.8×).

## Claims not supported

- ACIC is generally faster than GAPBS, Wasp, RIKEN or GPU SSSP systems: RIKEN
  wins on every scale-free graph, and Wasp wins on `road-usa-z` at every node
  count and on `mesh24-z`.
- Live feedback or algorithm/communication co-design causes the current win.
- High-diameter graphs generally favor ACIC: on the OSM roads ACIC at best ties
  Wasp, and on `road-usa-z` it loses to it.
- ACIC scales on roads beyond 16 Frontier nodes (1.1–1.2× from 16 to 64), or
  has any strong-scaling curve on Anvil.
- Results on inputs past one node's memory (pending: `road-planet-z` fits one
  node; `terrain-ae-z` does not).
- The current candidate is regression-free on RMAT. The Frontier frozen-binary
  gate returned NO-GO by its recorded rule (`rmat26`, `rmat27`, 2.5–5% on
  single sources, §10).
- Frontier launch bimodality is caused by LCI, the network or ACIC. Evidence
  localizes it only to remote tail progress.
