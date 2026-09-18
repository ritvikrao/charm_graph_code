# Step 8: make ACIC scale past two nodes

*2026-09-16. Follows [7.6o](step76-external.md#76o-scaling-comparison) and
comes before the old steps 8–12 (now 9–13). Scope: SSSP on scale-free
graphs at 2–8 nodes, where 7.6o failed. High-diameter graphs are a
regression check here, not a target.*

## What 7.6o showed

Job 20776357 ran 8 nodes and was compared with the 7.6n 2-node job
(20770615), each system at its own chosen layout:

| graph | ACIC 2 → 8 nodes | RIKEN 2 → 8 nodes | RIKEN lead 2 → 8 nodes |
|---|---|---|---|
| rmat25 | 0.64 → 0.80 s | 0.21 → 0.069 s | 2.8× → 11.7× |
| orkut | 0.23 → 0.32 s | 0.066 → 0.041 s | 3.5× → 7.5× |

ACIC gets slower with more nodes, and RIKEN gets faster. The counters say
why ACIC gets slower. On rmat25 it does much more work at 8 nodes:

| rmat25, ACIC's chosen layout | 2 nodes (224 PEs) | 8 nodes (896 PEs) | ratio |
|---|---:|---:|---:|
| updates delivered per graph edge | 1.22 | 4.60 | 3.8× |
| distance changes per vertex | 1.87 | 4.53 | 2.4× |
| bytes sent per graph edge | 9.7 | 36.9 | 3.8× |
| messages | 0.65 M | 4.12 M | 6.4× |
| controller rounds in the solve | 129 | 61 | 0.47× |
| idle flushes (one run) | 59 k | 2.02 M | 34× |
| stale-destination flushes (one run) | 264 k | 978 k | 3.7× |
| buffer bytes allocated / bytes sent (one run) | 2.9 | 5.1 | |

Rows without a note are medians over the four test sources. Rows marked
"one run" come from the first test run in each job, since the harness does
not parse those counters.

orkut behaves the same way: 1.66 → 5.15 updates per edge, 137 → 47 rounds.
The larger graphs grow less, because each PE has more local work: rmat26
goes from 1.09 to 2.12 updates per edge (242 → 71 rounds), rmat27 from 1.01
to 1.80 (316 → 113). There, ACIC does get faster from 2 to 8 nodes (1.07×
and 1.44×), but RIKEN gets faster by 2.2–2.3×, so its lead still grows
(2.9× → 5.6×, 2.5× → 4.1×). At 2 nodes, rmat26 and rmat27 spend 21–30% of
ACIC's time outside its own work, against 44–47% at 8.

The communication-share builds (`acic_comm`, `riken_sssp_mpit`,
[run.py `comm_share`](../benchmarks/run.py)) put every system at about half
communication at 8 nodes:

| 8 nodes | ACIC own work | ACIC in htram sends | ACIC everything else | RIKEN in MPI | Gluon sync |
|---|---:|---:|---:|---:|---:|
| rmat25 | 45% | 5% | 50% | 46% | 63% |
| orkut | 35% | 4% | 60% | 62% | 55% |
| rmat27 | 52% | 4% | 44% | 47% | 50% |

At 1 node, ACIC's "everything else" on rmat25 was 19%.

So the scaling argument's premise holds: at 8 nodes communication matters as
much as per-edge CPU. ACIC loses anyway, because it does about 4× the work
at 8 nodes that it does at 2, and spends half its time outside its own work.
RIKEN's work per edge was not counted, but it gets 1.6–3× faster from 2 to
8 nodes, and its design avoids redundant work. It:

- splits every bucket into light and heavy edge phases;
- shares bitmaps of settled vertices between ranks;
- switches to Bellman-Ford for the tail.

The first two are in its logs ("LIGHT/HEAVY phase", "expanded settled
vertices (bitmap)") and its source (`sssp.hpp`). The Graph500 bottom-up
(pull) machinery is also compiled in; whether the SSSP path uses it is
checked in 8a, which also counts RIKEN's edge relaxations at 2 and
8 nodes.

## Current comparison, all graphs

Median solve seconds over the four held-out sources, each system at its own
chosen layout and parameters (below). The jobs are 20770612 (1 node, 7.6n),
20770615 (2 nodes, 7.6n), 20776356 (2 nodes, 7.6o) and 20776357 (8 nodes,
7.6o). **Bold** marks the fastest distributed system in each row.

GAPBS runs on one node only. RIKEN cannot run road-usa: its distances exceed
the exact range of its binary32 distance type. mesh26 was prepared but not
run by 7.6n or 7.6o, and the high-diameter graphs were not run at 8 nodes.

| graph | nodes | ACIC | control (ACIC again) | RIKEN | Gluon-Async | GAPBS (1 node) | RIKEN lead | ACIC lead over Gluon |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| mesh24 | 1 | **0.669** | 0.797 | 26.5 | 4.49 | 0.190 | ACIC 38× | 7.2× |
| mesh24 | 2 | **0.658** | 0.636 | 17.7 | 3.73 | | ACIC 27× | 6.1× |
| road-usa | 1 | **1.86** | 1.81 | n/a | 26.0 | 0.152 | | 14.6× |
| road-usa | 2 | **1.57** | 1.68 | n/a | 38.6 | | | 24.2× |
| orkut | 1 | 0.257 | 0.281 | **0.106** | 0.846 | 0.156 | 2.4× | 3.1× |
| orkut | 2 | 0.230 | 0.258 | **0.066** | 1.46 | | 3.5× | 6.4× |
| orkut | 8 | 0.315 | 0.307 | **0.041** | 1.78 | | 7.5× | 6.0× |
| rmat25 | 1 | 1.40 | 0.915 | **0.323** | 1.79 | 1.16 | 4.3× | 1.2× |
| rmat25 | 2 | 0.637 | 0.698 | **0.206** | 1.96 | | 2.8× | 3.0× |
| rmat25 | 8 | 0.802 | 0.862 | **0.069** | 1.77 | | 11.7× | 2.2× |
| rmat26 | 2 | 1.13 | 1.12 | **0.389** | 2.79 | | 2.9× | 2.5× |
| rmat26 | 8 | 1.06 | 1.01 | **0.179** | 2.25 | | 5.6× | 2.2× |
| rmat27 | 2 | 2.02 | 2.05 | **0.805** | 5.79 | | 2.5× | 2.9× |
| rmat27 | 8 | 1.41 | 1.39 | **0.343** | 2.58 | | 4.1× | 1.8× |

How to read the table:

- **Leads** are paired medians over the same sources: ACIC time divided by
  the other system's time, or the reverse where marked "ACIC".
- **The control column** is ACIC's chosen configuration run again under
  another name, so it shows how far one ACIC time can move. The rmat25 row
  at 1 node is bimodal (0.9 or 1.4 s for the same configuration); every
  other control is within 1.2×.

Failures across the four jobs:

- **RIKEN:** 19 crashes, all of the riken-r16-d1024 setting. The search
  chose other settings.
- **20776356:** two launch failures in Slurm on the first (warmup) runs,
  for ACIC and Gluon ("Communication connection failure", "Duplicate job
  id"). Neither system had a wrong result or a hang.

One-node GAPBS is still faster than ACIC's best time at any node count on
every graph it ran except rmat25, where ACIC at 2 nodes (0.64 s) beats it
(1.16 s). GAPBS was not run on rmat26 or rmat27.

## Current ACIC build and settings

The build ran in 7.6n and 7.6o (`campaign/bin/acic`, sha256 `a6ef533d…`):

| item | setting |
|---|---|
| sources | the 7.6k–n working trees, committed afterwards as charm_graph_code `c7cfa79` and htram `6d7ddb7` (the manifest records `16738d6+dirty` and `0da4d3e+dirty`) |
| runtime | `charm_reconverse` `f6c74074f` with Reconverse `33b8c36`, built `reconverse-linux-x86_64 mpicxx --with-production --enable-shmem` (tree `-v0916-shm`; POSIX shared-memory pool 8 MB, cutoff 256 KB) |
| compiler | GCC 11.2.0, `-O3 -g`; OpenMPI 4.0.6 underneath |
| wire | `WIRE=compact`: 8-byte items, destination PE recomputed on arrival (7.6j) |
| aggregation | htram WPs, buckets by destination (`-DBUCKETS_BY_DEST`), `BUFSIZE` cap 16384 |
| buffer size | `--bufsize-policy acceptance` (7.6k): 256 × average degree, in steps of 256, within 512–6144; resized mid-run only when the arrival acceptance share asks for a 2× change. rmat and orkut run at 6144, mesh24 at 1024, road-usa at 512 |
| send filter | `--send-filter auto`: a 2^17-entry per-PE dominance table, on only while the buffer size is ≥ 2048 |
| destination lookup | one-load uniform table with fallback (7.6m) |
| admission | adaptive heap/TRAM thresholds from the reduced histogram; percentiles 0.999 (TRAM: sends) / 0.005 (heap: local expansion), in that positional order; initial threshold 3; histogram reduction width `HISTO_BUCKET_COUNT/8` |
| delivery | `--flush-policy adaptive` (gated cadence, interval 5 rounds), flush timer 0.01 ms, `--idle-flush starved` |
| buckets | `--bucket-policy adaptive` (coarsening, target 8, clamp blocked, frozen clamp), width `log(V)` except the mesh `weight` rule (`PER_GRAPH_WIDTH_RULE`, mesh20/22 only); `--pq-overflow-last on` (7.6g) |
| off | combining and batch fold, range extension, skew deferral |
| launch | `launch_acic.sh`: R processes per node, each pinned to its 128/R-core stride with one core left free (8×15 or 16×7 workers); `+lci_ndevices 4`; `--result-digest --timeout` |
| environment | `SLURM_MPI_TYPE=pmi2`, `PMI_MAX_KVS_ENTRIES=100000`, `FI_CXI_RX_MATCH_MODE=hybrid`, `NO_AFFINITY=1`, `OMP_PROC_BIND=close`, `OMP_PLACES=cores` |

Layouts that ACIC's search chose:

- 16 processes × 7 workers per node everywhere, except orkut at 2 and
  8 nodes and rmat26 at 8 nodes, which chose 8 × 15.
- Every A/B in step 8 starts from this build. Variants are built with
  `build_variant.sh` and recorded in `campaign/bin/variants-manifest.txt`.

Measurement builds, not used for timing:

- `acic_comm`: `-DACIC_COMM_SHARE`.
- `riken_sssp_mpit` with `mpi_share.so`.

## Baseline builds and chosen settings

All baselines use one toolchain (`scripts/anvil/build_baselines.sh`, GCC
11.2.0, OpenMPI 4.0.6 over UCX):

- **RIKEN** Graph500-SSSP `552f156`, `VERTEX_REORDERING=2`, with our reader
  and digest driver.
- **GAPBS** `2972aeb`.
- **Galois/Gluon** `b67f942` with `gluon.patch` (a timer and a digest only).

Each system searched its layout on one tuning source, then its parameters on
both tuning sources (7.6f1's two-stage search). The choices:

| graph | nodes | RIKEN | Gluon-Async | GAPBS |
|---|---:|---|---|---|
| mesh24 | 1 / 2 | 16 ranks × 7 threads, Δ 64/1024 | 1 rank, oec, Δ 1024 / 8 ranks, oec, Δ 1024 | 16 threads, Δ 1024 |
| road-usa | 1 / 2 | n/a | 1 rank, oec, Δ 0 / 1 rank, oec, Δ 32768 | 64 threads, Δ 32768 |
| orkut | 1 / 2 / 8 | 16 × 7, Δ 64/1024 | 1 rank, oec / cvc / cvc, Δ 64 | 64 threads, Δ 16 |
| rmat25 | 1 / 2 / 8 | 16 × 7, Δ 16/1024 | 1 rank oec Δ 1024 / 8 ranks cvc Δ 64 / 8 ranks oec Δ 64 | 127 threads, Δ 16 |
| rmat26 | 2 / 8 | 16 × 7, Δ 16/1024 | 8 ranks, cvc / oec, Δ 64 | |
| rmat27 | 2 / 8 | 16 × 7, Δ 16/1024 | 8 ranks, cvc Δ 1024 / cvc Δ 64 | |

Delta units differ by system:

- **RIKEN:** a fraction of the weight range (Δ 16/1024 means 16 of 1024),
  since its driver scales weights by that denominator.
- **GAPBS and Gluon:** integer weight units. Gluon's Δ is the step by which
  its priority bound rises each round, and Δ 0 removes the bound.

RIKEN picked the largest rank count offered (16 per node) every time, so its
times are an upper bound on what it can do.

## Hypotheses

The costs come from four suspects, which may overlap:

- **H5 Speculation.** With four times the PEs, more of them process buckets
  ahead of the true frontier before the controller tightens admission. The
  extra distance changes per vertex are this directly.
- **H6 Stream dilution.** There are 16× more (source PE, destination
  process) buffers, each fed 4–16× more slowly. Most messages leave through
  idle and stale flushes, not because a buffer filled. Updates wait longer,
  corrections arrive later, and wrong-order work grows. The 7.6k degree
  rule cannot see this. orkut's 1.13× miss is the same effect at 2 nodes.
- **H7 Controller latency.** A round costs O(PEs × histogram width), and at
  8 nodes the solve fits in less than half as many rounds. Admission reacts
  a round late, and the round is longer.
- **H8 Missing pruning.** ACIC relaxes heavy edges as soon as a vertex
  changes, and sends updates to vertices that are already final. RIKEN does
  neither.

## What's next

Each sub-step is an interleaved A/B (`ab_compare.sbatch`) at **8 nodes**,
with a 2-node regression check on rmat25 and orkut. All sub-steps use the
7.6o layouts. Digests are checked on every run, and the floor is measured in
each allocation.

| # | Step | Gate |
|---|---|---|
| 8a | **Attribute the growth.** rmat25 and orkut at 2/4/8 nodes on the `acic_diag` build (rounds.csv: thresholds, admitted work above the frontier, round latency; per-stream fill). Four arms at 8 nodes, each isolating one suspect: fixed buffer sizes 512/2048 (H6), reduction delay halved (H7), heap threshold one bucket tighter (H5), and a counter for arrivals at vertices already below the frontier (H8). Count RIKEN's edge relaxations with its verbose counters, and read whether its SSSP path pulls. | A table of work per edge, changes per vertex, flush mix and round latency against node count, with each suspect's share. Continue to the fixes the table implicates; drop the rest |
| 8b | **Buffering that follows fan-out (H6).** Size buffers by the observed items per destination stream per round, not by degree alone. Aggregate per source process (htram's node-level source buffers) so 7–15 PEs share one stream per destination. Flush when an item's age, not the idle loop, says so. | At 8 nodes the partial-flush share and messages per edge are at or below the 2-node levels; rmat25 and orkut faster; no 2-node regression (orkut should close its 1.13×) |
| 8c | **A controller whose cost does not grow with PEs (H5, H7).** Reduce only the active histogram window, pre-aggregated per process. Account for the rounds an update spends in flight when setting admission. | Round latency at 8 nodes within 1.5× of 2 nodes; distance changes per vertex at 8 nodes within 1.5× of 2 nodes |
| 8d | **Pruning (H8).** Light/heavy split: a vertex relaxes its heavy edges (weight above the bucket width) once, when its bucket is admitted as settled, not on every improvement. Then, only if 8a shows arrivals at final vertices dominate: a per-process settled bound or bitmap broadcast with the thresholds. A pull phase for dense rounds only if 8a finds RIKEN uses one. | Updates per edge on rmat25 at 8 nodes ≤ 2 (from 4.6); identical digests |
| 8e | **Time outside the solver's own work.** Idle polling, the per-idle destination scan, message allocation churn (5 bytes allocated per byte sent), reduction handling. Profile the `acic_comm` build's "everything else" with the instrumented backend first. | "Everything else" ≤ 30% at 8 nodes on rmat25 |
| 8f | **Per-edge CPU at 2 nodes** (110 vs RIKEN's 49 ns), only if the solver's own work is still over half the time after 8b–8e. | Per 7.6m's rules |
| 8g | **Re-take (completes 7.6o).** 2/8/16 nodes, scale-free (rmat25–27, orkut) and high-diameter (mesh26, road-usa), all four systems, same search, `THEN_COMM_SHARE=2`. | **Gate A** below |

8a comes first because it decides which of 8b–8d are worth building. 8b and
8d are the likely large ones. Each is a change in either the library or the
algorithm, driven by the controller's signal: the fill observed per stream,
or the frontier. If either wins, that is the co-design test that 7.6k could
not provide, and it gets the same ablation (controller-driven vs fixed)
before it is claimed.

## 8a results (2026-09-18)

Jobs 20818217 (2 nodes), 20818218 (4), 20818219 (8), with 20818202 (one
node, smoke), on the first 7.6o test source of each graph (rmat25 26007212,
orkut 2549343), at the 7.6o layouts. The counters come from `acic_diag8`
(`-DACIC_DIAG -DVCOUNT` plus the 8a counters, one run per cell). The arms
come from the plain build, interleaved over three rounds. Every digest
matched. Output is in `campaign/step8a/`, and the job is
`scripts/anvil/step8a.sbatch`.

### Where the work goes

| | 1 node | 2 nodes | 4 nodes | 8 nodes |
|---|---:|---:|---:|---:|
| rmat25 updates delivered per edge | | 1.08 | 1.66 | 3.13 |
| rmat25 heavy updates per heavy edge of a reached vertex | | 1.30 | 1.76 | 3.72 |
| rmat25 expansions / least possible | | 1.01 | 1.03 | 1.23 |
| rmat25 arrivals at final vertices, share of arrivals | | 31% | 0.2% | 29% |
| **RIKEN rmat25 relaxations sent per edge** | | **0.64** | **0.50** | **0.50** |
| orkut updates delivered per edge | 1.37 | 2.11 | 2.33 | 4.41 |
| orkut heavy updates per heavy edge | 1.50 | 2.20 | 2.67 | 6.34 |
| orkut expansions / least possible | 1.25 | 1.61 | 1.83 | 4.44 |
| orkut distance changes per vertex | 6.0 | 8.3 | 8.5 | 13.6 |
| **RIKEN orkut relaxations sent per edge** | | **0.28** | **0.25** | **0.25** |
| longest controller round, rmat25 / orkut | — / 103 ms | 156 / 92 ms | 282 / 50 ms | 189 / 94 ms |

Definitions:

- **Heavy:** an edge longer than one natural bucket width (log V: 17.3 for
  rmat25, 14.9 for orkut), RIKEN's own cut. Weights are uniform on
  [1, 1000], so 98.3–98.6% of edges are heavy.
- **Least possible:** each reached vertex expanded once, at its final
  distance.
- **Arrivals at final vertices:** the target's distance was already below
  the frontier bucket that its PE last heard of. The 4-node figure is low
  because that run's frontier sat in bucket 0.
- **RIKEN's count:** every edge it hands to its send path
  (`top_down_send`, `top_down_send_large`), from a counting copy of its
  source (`deps/riken-count`, binary `riken_sssp_verbose`).

Readings:

1. **RIKEN sends fewer relaxations than there are edges.** It relaxes a
   heavy edge once, from a settled vertex, and skips targets its
   settled-vertex bitmap marks final. ACIC sends 2–6× RIKEN's count at
   2 nodes and 6–25× at 8.
2. **RIKEN's SSSP never pulls.** Its SSSP path asserts on the backward
   direction ("backward currently not implemented"). It runs 3–4 buckets of
   light iterations with one heavy phase each, then switches to
   Bellman-Ford. So the pull phase in 8d is dropped.
3. **On rmat25 the growth is hubs expanded at distances that are not final.**
   Expansions grow only 1.01× → 1.23× from 2 to 8 nodes, but heavy updates
   grow 1.30 → 3.72 per heavy edge. The vertices that get re-expanded are the
   high-degree ones, and every re-expansion resends all their heavy edges.
4. **On orkut the growth is broad re-expansion.** Vertices are expanded 4.4×
   at 8 nodes and change distance 13.6 times each. After round 2 the
   controller has coarsened buckets 9–20×, to 135–300 distance units. That is
   more than the typical orkut distance, so nearly all live work sits in one
   bucket and there is no global order left.
5. **The controller is blind while the flood happens.** On orkut at one node,
   94% of all updates are created in rounds 3–6, which take 25–100 ms each.
   At 2–8 nodes the longest round is 50–282 ms against a median of 1–3 ms.
   The round's broadcast waits in the PE queues behind the data.

### The suspect arms at 8 nodes

Medians of three runs:

| arm | rmat25 s | updates/edge | messages | items/message | orkut s | updates/edge | messages | items/message |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| base (acceptance buffer size: 6144) | 0.836 | 4.78 | 4.42 M | 1165 | 0.313 | 4.89 | 2.13 M | 509 |
| buffer size 512 (H6) | **0.572** | 2.92 | 7.73 M | 389 | 0.371 | 4.31 | 4.44 M | 268 |
| buffer size 2048 (H6) | 0.641 | 3.51 | 3.96 M | 914 | **0.269** | 3.62 | 2.05 M | 416 |
| TRAM (send) percentile 0.9 (H5) | 0.771 | 4.42 | 4.33 M | 1068 | 0.432 | 4.97 | 2.17 M | 506 |
| TRAM (send) percentile 0.5 (H5) | 0.773 | 4.51 | 3.33 M | 1442 | 0.284 | 4.53 | 1.89 M | 548 |
| bucket target 32, finer ordering (H5) | 1.126 | 4.54 | 7.62 M | 610 | 0.356 | 4.13 | 2.48 M | 434 |
| 0.5 ms added per round (H7) | 0.851 | 4.90 | 3.51 M | 1380 | 0.346 | 5.22 | 2.49 M | 469 |

The job labels the two percentile arms "heap90" and "heap50". The
positional order is TRAM percentile, then heap percentile, so they moved
the TRAM percentile. The table above at "Current ACIC build and settings"
had the two swapped and is corrected.

Each suspect's share:

- **H6 (buffering): implicated.**
  - Buffer size 512 cuts rmat25's work per edge by 39% and its time 1.46×.
  - orkut wants 2048: 1.16× faster. It is slower at 512 (4.4 M messages).
  - So the latency of a partly filled buffer is what makes a hub's first
    distance wrong on rmat25. No single fill-rate rule fits both graphs:
    rmat25 is fastest at about one item per 20 edges per stream at both 2 and
    8 nodes, but orkut at 8 nodes would get about 190 by that rule and is
    fastest at 2048.
- **H8 (pruning): implicated.** RIKEN's structure accounts for a 6–25× gap
  in relaxations sent.
- **H5 (speculation): weak.**
  - A tighter TRAM percentile gains 1.08–1.10×.
  - Finer buckets on their own are slower. On rmat25 they give 4× the rounds
    and 1.7× the messages, with no less work.
  - Ordering does not help unless the waste is pruned.
- **H7 (controller cost): not implicated as posed.** Adding 0.5 ms per round
  costs at most 1.1×. A round costs what the queue in front of its broadcast
  costs, and that is a symptom of the waste, not of the reduction's
  O(PEs × width).

**Consequence for the plan.**

- 8d moves ahead of 8b. The waste is what makes buffer latency expensive and
  what floods the controller, and RIKEN's 0.25–0.64 relaxations per edge is
  the target.
- 8b is re-tuned after 8d, measured as the node count rises.
- 8c drops its "reduce only the active window" half. It keeps the in-flight
  accounting only if the rounds are still blind after 8d.
- 8d's pull phase is dropped (reading 2).

## 8d results: lazy heavy relaxation (2026-09-18)

`--lazy-heavy` (commit 51db6d9) works as follows:

- **Light edges now.** A vertex relaxes its edges of weight up to L as soon
  as its queued distance is current.
- **One token per heavier range.** The rest of its edges are left behind
  tokens, one per weight range (L, 2L], (2L, 4L], and so on.
- **Queued like an update.** The token for range j is queued at d + L 2^j
  and charged to the histogram as if it were an update there. The heap
  threshold therefore admits it exactly when it would admit the first update
  the token can make.
- **Stale tokens are dropped.** When a token is admitted and its vertex has
  moved since, the token is dropped; otherwise it relaxes its range and
  queues the next.
- **What does not change.** The frontier stays monotone, and termination
  reads the same created and processed counts.

With L the natural bucket width, it sends each heavy edge 1.01–1.16 times
at every node count measured, against 1.30–6.34 before. The runs are
`campaign/step8a/*-{20818305,20818307,20818316,20820531,20820559,20820560,20820633}`,
medians of three runs, one source per graph.

| 8 nodes | old build | lazy, heap 0.005 | lazy, heap 0.5 | lazy, heap 0.95 |
|---|---:|---:|---:|---:|
| rmat25 s | 0.759–0.883 | 0.804–0.808 | 0.469–0.506 | **0.424** |
| rmat25 updates / edge | 4.30–4.93 | 1.04–1.27 | 1.07–1.15 | 1.01 |
| rmat25 rounds × median round | 58–62 × 1.3–1.5 ms | 105–147 × 2.5–2.8 ms | 57–58 × 5.4–7.2 ms | 62 × 2.8 ms |
| orkut s | 0.294–0.323 | 0.405–0.422 | 0.226 | **0.188** |
| orkut updates / edge | 4.71–5.11 | 0.74 | 0.76–0.77 | 0.81 |
| orkut rounds × median round | 43–46 × 0.6–1.0 ms | 135 × 2.3 ms | 46 × 4.3 ms | 29 × 6.7 ms |

| 2 nodes | old build | lazy, heap 0.005 | lazy, heap 0.95 | lazy L=64, heap 0.5 |
|---|---:|---:|---:|---:|
| rmat25 s | 0.593–0.640 | 0.839 | 0.610 | 0.644 |
| orkut s | 0.235–0.241 | 0.212 | 0.182 | **0.151** |
| mesh24 s | 0.995 | 2.157 | 2.247 | |
| road-usa s | 2.630 | 5.611 | 5.540 | |

Readings:

1. **Pruning works.** Work per edge on the scale-free graphs falls below
   one, near RIKEN's range. With the positional heap percentile (0.005) the
   time does not follow, because the solve becomes paced by the controller:
   about 2.5× the rounds, each longer.
2. **A wider heap percentile recovers the time.** Tokens make running ahead
   cheap, so fewer, wider rounds pay, and at 0.95 the work stays at one
   update per edge. At 8 nodes rmat25 is 1.79× faster than the old build
   and orkut 1.72×. rmat25 is now faster at 8 nodes than at 2 (0.42 against
   0.61 s); orkut is flat (0.19 against 0.18 s).
3. **RIKEN's lead at 8 nodes would be 6.1× on rmat25 and 4.6× on orkut,**
   against 11.7× and 7.5×. This uses RIKEN's 7.6o medians and one ACIC
   source, so it is not a gate reading.
4. **High-diameter graphs lose 2.1×.** There every edge is heavy, and
   deferring them lengthens the improvement chains: distance changes per
   vertex go from 18 to 84. `--lazy-heavy auto` is therefore on only for
   average degree ≥ 8, the send filter's scale-free line. `--lazy-heap`
   (default 0.95) is the heap percentile used only while lazy relaxation is
   active. The wide heap alone, without lazy, costs mesh24 and road-usa
   about 5%.
5. **The rounds are now the critical path.** At 8 nodes, rounds × median
   round is most of the solve. Reconverse's scheduler polls the node queue
   (all of htram's traffic) before a PE's own queue, which holds the
   threshold broadcast, the reduction's tree messages and the per-PE share
   of every delivery. So a round waits for the whole backlog at every hop.
   That is 8c's target.

**Reconverse `adaptive-queues`** (merged onto 33b8c36 as local branch
`step8-adaptive-queues`, `~/reconverse-worktrees/step8-aq`; Charm++ tree
`-v0918-aq`) polls a PE's own queues 16 times for each poll of the node
queue and re-weights the table from the work each queue yields.
`register-queues` has the same static weights.

On orkut at one node (job 20820727, one run each) it is 4–6× slower:

| | lazy off | lazy on |
|---|---:|---:|
| 7.6k–n runtime | 0.267 s | 0.306 s |
| `-v0918-aq` | 1.541 s | 1.127 s |
| work per edge, `-v0918-aq` | 1.33 → 7.50 | 0.78 → 4.48 |
| longest round, `-v0918-aq` | 1148 ms | 524 ms |

htram's deliveries then wait in the node queue instead. Re-weighting whole
queues moves the starvation from the control messages to the data. What
8c needs is for the control messages alone to go first, so it builds a
control path on the node queue in the application.

## 8c results: the control path (2026-09-18)

`--control node` (off by default) takes the controller's messages out of
the PEs' own queues. It works as follows:

- **Contributions are summed per process.** The PEs of a process sum their
  round contributions in shared memory (`ControlNode::add`), and the last
  one sends the sum to process 0 on the node queue.
- **Thresholds come back to shared state.** Process 0 broadcasts them to
  every process on the node queue, where they wait in shared memory.
- **Each PE picks the round up at its next opportunity:** a delivery, a heap
  pass or an idle pass. A queued `pickup_fallback` message covers a PE that
  reaches none of these first.

The first version livelocked on one PE. There a round followed every
delivery, and each round queued a heap pass, a hold clear and a fallback
message, so the PE's queue grew faster than it drained. The reduction path
had been paced by its own wait in that queue. So now:

- a PE keeps at most one heap pass and one fallback message queued;
- it clears admitted holds directly;
- process 0 leaves at least `--control-interval` ms (default 0.25) between
  broadcasts.

The verify gate passes with it, alone and with `--lazy-heavy on`.

**It is slower.** Jobs 20820853 (8 nodes) and 20820854 (2 nodes), medians
of three runs, lazy auto throughout:

| | reduction | node, 0.05 ms | node, 0.25 ms | node, 1 ms |
|---|---:|---:|---:|---:|
| rmat25, 8 nodes | **0.401 s** (44 rounds × 2.5 ms) | 0.625 s (85 × 6.2 ms) | 0.555 s (74 × 4.0 ms) | 0.566 s (96 × 6.0 ms) |
| orkut, 8 nodes | **0.189 s** (34 × 2.2 ms) | 0.228 s | 0.214 s | 0.213 s |
| rmat25, 2 nodes | **0.626 s** | 0.739 s | 0.748 s | 0.802 s |
| orkut, 2 nodes | **0.193 s** | 0.279 s | 0.264 s | 0.204 s |

Readings:

- **The median round at 8 nodes does not get shorter.** A round still waits
  for the slowest of 896–960 PEs to reach a pickup point. That can be a
  heap pass that expands a hub, or a delivery of several thousand items.
  The queue order was not what set it.
- **The extra rounds cost stale flushes.** rmat25 at 8 nodes sends
  2.7–3.5 M stale flushes against 1.4 M, in smaller messages.
- **With lazy relaxation and heap 0.95 the rounds are no longer most of
  the solve.** On rmat25 at 8 nodes, 44 rounds of about 2.5 ms are about
  110 ms of 401. So 8c's other half (accounting for rounds in flight) is
  not pursued, and the time goes to 8e.

## 8e results: time outside the solver's own work (2026-09-18)

**Where the time went.** With lazy relaxation on, the communication-share
build (`acic_comm8`; jobs 20820912–13) put the solver's own work at 8 nodes
at 33–38% of PE time on rmat25 and 17% on orkut, against 67–72% and 40% at
2 nodes. Work is balanced: the busiest PE processes 1.1–1.3× the median.
The PAPI sampling profile (`acic_papi8`, `papi_profile.sbatch`, jobs
20820957–58) shows where the rest goes on rmat25 at 8 nodes:

| function | share of cycles |
|---|---:|
| `HTram::flushIdle` | 20% |
| `HTram::flushDest` | 18% |
| `HTram::changeThreshold` | 4.5% |
| `HTram::coarsenBuckets` | 2.9% |
| all of the solver's own functions | about 16% |

`flushDest`, `tflush`, `insertBucketsByDest`, `changeThreshold`'s recount,
`coarsenBuckets` and the `ADD_FILLERS` padding each walked a destination's
hold one bucket at a time, empty or not. That is up to 2048 queues per
destination per flush, for 128 destinations at 8 nodes.

**Fix 1: the hold bitmap** (htram `5d10533`). One bit per (destination,
bucket) marks a hold queue that has items, and every one of those loops now
visits only the set bits. The verify gate passes. Jobs 20821010–11:

| | old | lazy, no bitmap | lazy + bitmap |
|---|---:|---:|---:|
| rmat25, 8 nodes | 0.867 s | 0.398 s | **0.244 s** |
| rmat25, 2 nodes | 0.652 s | 0.623 s | 0.540 s |
| orkut, 8 nodes | 0.317 s | 0.192 s | 0.197 s |
| orkut, 2 nodes | 0.240 s | 0.197 s | 0.152 s |

On rmat25 at 8 nodes `flushDest` falls to 1.6% of cycles and the solver's
own work leads the profile (job 20821031).

**The side effect.** The bitmap made road-usa at 2 nodes 2× slower:
1.73 s against 3.48 s, with 31 M idle flushes against 7.4 M (job
20821099). The same source without the bitmap (`acic_8c`) ran 1.93 s, and
with idle flushing off 1.88 s. So the slow drain had been pacing the idle
flush: an idle PE flushes every destination that holds anything, on every
scheduler pass.

**Fix 2: an idle-flush interval.** `--idle-flush-interval`
(`HTram::setIdleFlushInterval`) sets the least time between two idle
flushes of a PE that sent something. Jobs 20821256–57, medians of three:

| | old | 0 µs | 10 µs | 30 µs | 100 µs | idle flush off |
|---|---:|---:|---:|---:|---:|---:|
| rmat25, 8 nodes (lazy) | 0.777 | 0.266 | 0.220 | **0.212** | 0.259 | 0.253 |
| orkut, 8 nodes (lazy) | 0.339 | 0.247 | 0.217 | **0.202** | 0.217 | 0.249 |
| road-usa, 2 nodes | 1.680 | 3.332 | 2.833 | 1.588 | **1.476** | 1.917 |
| mesh24, 2 nodes | 0.579 | 0.559 | 0.552 | 0.547 | **0.441** | 0.459 |

The default, `auto`, is 30 µs at average degree ≥ 8 and 100 µs below, the
same regime line as `--lazy-heavy auto`, which is now the default too.
Against the 7.6k–n build (`campaign/bin/acic`) these defaults are:

- rmat25 at 8 nodes: 3.7× faster;
- orkut at 8 nodes: 1.7× faster;
- mesh24 at 2 nodes: 1.3× faster;
- road-usa at 2 nodes: 1.1× faster.

**What is left, for orkut at 8 nodes** (profile job 20821030):

- 58% of cycles are the scheduler and network progress: Reconverse's
  shared-memory poll (`CmiPopIpcBlock`, 14%), LCI progress (9%), the
  scheduler loop and its queue dequeue. The PEs are waiting.
- Each PE has about 200 K updates to do.
- Its buffers to 64 destination processes fill at about 19 K items/s each,
  so every message leaves by idle or stale flush, about 70 items at a time.
- The solve advances in flush waves, and orkut is slower at 8 nodes than at
  2 (0.20 against 0.15 s).
- Fewer processes per node, which means fewer and fuller streams, are worse:
  4 per node 0.54 s, 2 per node 1.05 s (jobs 20821055–56). That is left
  to 8b.

## 8b so far: empty deliveries, and the entry check (2026-09-18)

htram's node-level receive sorts a message by destination PE, then sends
one per-PE delivery to *every* PE of the process, whatever that PE's share.
At 8 nodes a message averages about 70 items over 15 PEs, so most
deliveries carry nothing.

Skipping the empty ones had opposite effects by regime (jobs
20821360–62, 20821390–91):

- **Scale-free:** orkut at 8 nodes 1.1× faster (0.180 against 0.202 s),
  neutral at 2 nodes.
- **High-diameter:** mesh24 at 2 nodes 2.1× slower (0.91–1.02 against
  0.44 s) and road-usa 4.4× slower. Updates per edge rose from 12.5 to 20.

The empty deliveries sit in each PE's FIFO ahead of its heap-pass
continuations. So more improvements are applied before a vertex is
expanded, and a high-diameter graph's long improvement chains depend on
that. It is an accident of scheduling, not a design. The skip is now an
htram option (`setSkipEmptyDeliveries`), and ACIC turns it on in the
scale-free regime only, with lazy relaxation.

**Entry check, one allocation per node count** (jobs 20821480 at 8 nodes
and 20821481 at 2 nodes):

- the current defaults (`acic_8b2`), the 7.6k–n build and RIKEN at its
  7.6o setting;
- run interleaved on the four 7.6o test sources;
- medians, with leads as paired medians.

| | ACIC now | ACIC 7.6k–n | RIKEN | RIKEN lead now | RIKEN lead, 7.6k–n build | now vs 7.6k–n |
|---|---:|---:|---:|---:|---:|---:|
| rmat25, 8 nodes | 0.259 s | 0.852 s | 0.072 s | **3.59×** | 11.8× | 3.60× |
| orkut, 8 nodes | 0.175 s | 0.303 s | 0.032 s | **4.85×** | 9.46× | 1.80× |
| rmat25, 2 nodes | 0.558 s | 0.659 s | 0.196 s | 2.78× | 3.35× | 1.17× |
| orkut, 2 nodes | 0.150 s | 0.215 s | 0.067 s | 2.25× | 3.35× | 1.45× |

Against the entry condition:

- rmat25 is now faster at 8 nodes than at 2 (0.56 → 0.26 s); orkut is not
  (0.15 → 0.175 s).
- RIKEN's lead at 8 nodes is still above 3× on both.
- The high-diameter graphs are faster than with the 7.6k–n build.

Not met yet. The stop rule (RIKEN more than 5× ahead on rmat25) is not
triggered.

**Settled pruning, measured again under the new defaults.** 8a counted
70–75% of orkut's arrivals landing on vertices already below the frontier.
Those were lazy runs at heap percentile 0.005. At the current heap
percentile of 0.95 the same counter reads 0.1% on rmat25 and 4.4% on orkut
at 8 nodes (`acic_diag8h`, `STEP8A_SETTLED_BY_DEGREE`, job 20822160). The
frontier now sits far behind the work, so a sender that knew which hubs are
settled would skip almost nothing. The settled-hub filter planned for 8d
was written but never built, and was taken out. RIKEN's bitmap works
because its buckets are narrow and its phases synchronous.

## What else was tried for orkut at 8 nodes, and 8f (2026-09-18)

**The idle share.** The communication-share build now also times the
scheduler's idle intervals, less the solver work its idle callback did
(`idle_share`; job 20822844). At 8 nodes with the current defaults:

| | own work | sends | idle | other runtime |
|---|---:|---:|---:|---:|
| orkut | 14–18% | 5–6% | **50–61%** | about 20% |
| rmat25 | 50–56% | 6% | 21–29% | about 15% |

So orkut at 8 nodes mostly waits. Three explanations were tested, and all
three failed:

- **Flushing on age, on busy PEs** (`--busy-flush-age`, built and reverted;
  job 20823299). The idea was that busy PEs hold partial buffers the idle
  PEs are waiting for. It is 2–4× slower. At 8 nodes a buffer holds only
  13–18 items by the time its oldest has waited 10–100 µs, so messages go
  from 2–3 M to 11–85 M.
- **Warming the links before the solve** (`--warm-links`, now off by
  default; job 20823450). ACIC's setup exchanges nothing between processes,
  so the first messages meet cold connections inside the timed solve. The
  warm-up shortens rmat25's first round from 11.8 to 5.3 ms, but not the
  solve: 0.250 → 0.235 s on rmat25, 0.156 → 0.169 s on orkut.
- **Fewer processes per node** (jobs 20821055–56), for fewer and fuller
  streams. 4 per node: orkut 0.54 s. 2 per node: rmat25 4.7 s.

**8f, the solver's own work.** It is over half the time only on rmat25 at
8 nodes. Queue operations (`process_heap`, `__adjust_heap`) are about 20%
of its cycles, and lazy relaxation adds about 4.4 tokens per vertex.
`--lazy-growth G` widens the token ranges to (L G^j, L G^(j+1)] (jobs
20823532–33):

| | G = 2 | G = 4 | G = 8 |
|---|---:|---:|---:|
| rmat25, 8 nodes | **0.237 s** | 0.295 s | 0.308 s |
| orkut, 8 nodes | **0.146 s** | 0.156 s | 0.199 s |
| rmat25, 2 nodes | 0.549 s | **0.506 s** | 0.510 s |
| orkut, 2 nodes | 0.155 s | 0.134 s | **0.129 s** |

Fewer tokens help at 2 nodes, where the queue is deep. The finer deferral
wins at 8, so the default stays at 2. A faster queue (a 4-ary or bucketed
heap) is worth at most the 20%, so it is left for step 9.

## Entry condition for 8g, and Gate A

8g runs above 8 nodes only if, at 8 nodes in one allocation:

- ACIC is faster at 8 nodes than at 2 on rmat25 and orkut;
- RIKEN's lead is at most 3× on both (from 11.7× and 7.5×);
- mesh24 and road-usa do not regress against 7.6n.

Gate A then reads as the [plan](sc27-plan.md#gate-a-checkpoints) states it,
on 8g.

**Stop rule.** If 8b–8d are all built and RIKEN still leads by more than 5×
at 8 nodes on rmat25, stop scale-free work. The remaining question is then
the narrower paper: high-diameter graphs, where ACIC was 6–38× ahead of both
distributed baselines at 1–2 nodes in 7.6n, re-taken at 8–16 nodes.

## Budget

An 8-node A/B slot costs about 50 SU for three minutes. Allow about 2,000 SU
for 8a–8f (8a about 400) and about 2,500 SU for 8g, against 69.9k SU
remaining on 09-16. Queue waits dominate: submit an 8-node A/B only after
the arms have passed a 1-node debug smoke test.

## Tools in place

- `acic_comm` (`-DACIC_COMM_SHARE` in `CHARMC_SMP`) prints a `COMM_SHARE`
  line. htram times each send (`HTRAM_TIMED_SEND`).
- `riken_sssp_mpit` + `mpi_share.so` time RIKEN's MPI calls inside
  `MPI_Pcontrol(1/0)`. Polling between `MPI_Test*` calls is not counted, so
  the figure is a lower bound.
- `run.py` parses Gluon's own sync timer on every run.
- `benchmarks/report_scaling.py LOGS JOB...` builds the node-count and share
  tables.
- `scripts/anvil/build_variant.sh` builds side-by-side variants;
  `ab_compare.sbatch` interleaves them.
