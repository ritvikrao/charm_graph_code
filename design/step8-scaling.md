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
| admission | adaptive heap/TRAM thresholds from the reduced histogram; percentiles 0.999 (heap) / 0.005 (TRAM); initial threshold 3; histogram reduction width `HISTO_BUCKET_COUNT/8` |
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
