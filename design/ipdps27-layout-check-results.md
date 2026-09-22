# Fixed-layout scaling check: results (Anvil, 2026-09-21)

This reports the [pre-recorded check](ipdps27-layout-check.md). All jobs
passed validation:

- the 224-solve gate on the new runtime;
- 80 solves in each 1- and 2-node job and 64 in each 4-node job, each
  digest-checked with its layout verified.

The total was **97.6 SU**, including four 1/2-node jobs that failed at
their first launch and were resubmitted (next section).

**Headline:** the updated Reconverse is a large regression on road, with
matching extra work. Measured on the new runtime, fixed layouts give road
**no scaling path**: its best time gets worse from one to four nodes. Mesh
scales with 16 × 7 at every node count. Both conclusions are conditioned on
the regression, and on high-diameter graphs only; see the RMAT scope below.

## 1. The runtime update regresses road

The same batch-8 sources (`48ca9c0`) were built on each runtime and run in
the same allocations. The table gives new/old ratios per source, for
allocations A and B.

| Cell | Graph | Time | Edge attempts |
|---|---|---|---|
| 1 node 16 × 7 | road | 1.28–1.41 | 1.31–1.45 |
| 1 node 16 × 7 | mesh | 1.03–1.05 | 1.04–1.06 |
| 2 nodes 8 × 15 | road | 1.46–1.53 | 1.46–1.62 |
| 2 nodes 8 × 15 | mesh | 1.06–1.22 | 1.11–1.18 |

The repeated controls agree with their primary arms within about 5%. The
old-runtime cells reproduce the attribution experiment: road 1-node 16 × 7
at 0.42–0.56 s and 2-node 8 × 15 at 0.47–0.63 s. So the new runtime makes
ACIC do more rework, rather than making the same work slower. Time follows work.

- **Not the spanning-tree default.** Commit `1233130` turns SPANTREE on only
  for new CMake caches. The v0921 build was configured from the old cache,
  so it still has `SPANTREE 0` in its generated `converse_config.h`.
  Spanning-tree broadcast was off in every run here.
- **Remaining suspects:** the other nine commits between `33b8c36` and
  `1233130`. The ones that could change message timing, and hence
  asynchronous rework, are:
  - `146ec42`, queue registration in the scheduler;
  - `c1070b5`, collectives fanning out once per destination process;
  - `4a68644`, the header growing from 20 to 24 bytes;
  - `e63d206`, the message-manager revert.

  A bisection over these would take about four 1-node builds and one short
  job each. It has not been run.
- **Binary compatibility.** ACIC binaries load `libreconverse.so` through
  `~/charm_reconverse/lib`. Moving that link made every binary built before
  the update run against the new library, and they segfault in
  `HTram::receivePerPE` because of the header change. That caused the four
  failed launches. The control copy `acic_r1_batch_rc0916` now has its
  RPATH set to the v0916 libraries with `patchelf` 0.19.1 (sha256
  `0814a59d…`), and it passes serial verification. Every other Anvil binary
  built before 2026-09-21 19:18 must be rebuilt, or patched the same way.

## 2. Layouts on the new runtime

These are the medians of per-source medians, in seconds, with edge attempts
per reachable edge. Allocation A is shown; B agrees within 1–6% in every cell.

| Nodes | Graph | 8 × 15 | 16 × 7 | 8 × 7 |
|---:|---|---|---|---|
| 1 | mesh | 1.193 / 1.67 | **0.943** / 1.68 | 1.407 / 1.42 |
| 2 | mesh | 0.805 / 2.20 | **0.679** / 2.37 | 0.882 / 1.70 |
| 4 | mesh | 0.664 / 3.59 | **0.542** / 3.51 | 0.629 / 2.37 |
| 1 | road | 0.679 / 3.45 | 0.656 / 4.19 | **0.625** / 2.27 |
| 2 | road | 0.813 / 8.42 | 0.916 / 11.88 | **0.613** / 4.33 |
| 4 | road | 1.248 / 25.43 | 1.510 / 38.62 | **0.821** / 11.87 |

Scaling uses the best layout per source and node count against the best at
one node:

| Graph / source | 2 nodes | 4 nodes |
|---|---|---|
| mesh 22442342 | 1.33× | 1.66–1.70× |
| mesh 41856222 | 1.46× | 1.85–1.86× |
| road 1294456 | **0.97–0.98×** | **0.70–0.72×** |
| road 5620086 | 1.06× | **0.79–0.81×** |

## 3. Predictions

1. **Road: confirmed, and stronger than predicted.**
   - 8 × 7 beats 8 × 15 by 25% at 2 nodes and 34% at 4 nodes.
   - The best at 4 nodes is not merely below 1.15× the best at 2 nodes; it
     takes 26–36% more time.
   - At 4 nodes even the least-concurrent layout, 8 × 7 (224 workers),
     does 10–14 attempts per edge. That is 5× its one-node work, and about
     40% more than 2-node 8 × 15 with a similar worker count (240). At four
     nodes, spreading processes now adds work beyond concurrency alone. On
     the old runtime at two nodes, placement was neutral (P ≈ 1).
   - Fixed layouts cannot give road useful multi-node scaling. Road needs
     breadth control, or the road route stops.
2. **Mesh: disconfirmed as to layout; scaling confirmed.** 16 × 7, not
   8 × 15, is best at every node count. Time keeps falling: 1.33–1.46× at
   2 nodes and 1.66–1.86× at 4. Mesh work also grows, from 1.7 to 3.5
   attempts per edge at 16 × 7.
3. **One node: split by graph.**
   - Mesh reproduces 16 × 7 as fastest, 16–21% ahead of 8 × 15.
   - On road, on the new runtime, 8 × 7 is slightly faster (0.62–0.63 s
     against 0.64–0.66 s).
   - Road's work optimum at one node is 8 × 7: 2.27 attempts per edge,
     against 3.45 at 8 × 15.
4. **Runtime change within noise: strongly disconfirmed** (section 1).

## 4. Consequences

- **Fixed baseline for any breadth-control intervention (new runtime):**
  - road: 8 × 7 per node at 1, 2 and 4 nodes;
  - mesh: 16 × 7 per node at 1, 2 and 4 nodes.

  This is layout tuning, not a mechanism.
- **Road now needs two things before any C6 attempt:** the runtime
  regression identified and fixed, and breadth control. Even the old
  runtime's best road time, about 0.42–0.56 s at one node, is 2–3× Delta's
  one-node GAPBS (0.17–0.20 s, a cross-machine comparison).
- **Mesh has a real scaling path.** 16 × 7 at 4 nodes is at 0.48–0.60 s.
  Delta's one-node GAPBS on mesh26 is 0.41 s, a cross-machine comparison to
  be re-measured on Anvil. Eight nodes at 16 × 7 is the natural mesh pilot.
- **Order of work:** bisect the runtime regression before more performance
  runs, because every new-runtime measurement carries it. Alternatively,
  pin performance work to v0916 until it is fixed.

## Scope: these conclusions do not transfer to RMAT

Every conclusion above comes from two high-diameter graphs.

- On the RMAT/scale-free regression graphs, `--process-share auto` resolves
  off. The shared queues and batch-8 setting studied here are then
  inactive, so the layout effects measured here, which work through shared
  queues and global concurrency, do not apply.
- Those graphs have wide frontiers that support far more workers; GAPBS
  tuning itself picks 128 threads on mesh26 but 64 on road.
- Their cost is dominated by hubs and aggregation. More processes per node
  (16 × 7) change htram's per-process aggregation and buffer fill, and
  could help or hurt them.
- The runtime regression may also differ there. It was only measured here.

A layout rule or concurrency limit drawn from this check must pass the
five-graph regression suite (`rmat25`, `orkut`, `uniform25`, `rmat26`,
`rmat27`) at its node counts before adoption. Any per-class rule must key on
metadata available when the graph is read, such as degree CV or the density
measure that already drives the auto modes, never on graph names.

## Evidence

- Audits: `onenode-data/layout-check-anvil-{20840725,20840726,20840042,20840727,20840728,20840045}.json`
- Gate: job 20840039
- Raw logs: `/anvil/scratch/x-rrao/acic/ipdps27-layout/logs/`

The failed first attempts (20840040/41/43/44) are kept there as well.

## Follow-up: `+old-scheduler` (recorded before submission)

The author pointed out that the new Reconverse can fall back to its original
scheduler with `+old-scheduler`. That flag disables the queue registration
from `146ec42`; the runtime prints "Using the original scheduler". The
follow-up runs the new-runtime binary with and without the flag, beside the
v0916 binary, at the two regressed cells: 1 node 16 × 7 and 2 nodes 8 × 15.
It covers mesh and road, with a repeated `+old-scheduler` control, in two
2-node allocations.

- **Prediction:** if queue registration causes the regression, the
  `+old-scheduler`/v0916 ratios for time and attempts fall within control
  noise, about 5%, on road in both allocations.
- **Otherwise:** a remaining gap points to the other commits (collective
  fan-out, header size, message manager), and bisection proceeds over those.

Config: [oldsched-variants.json](../benchmarks/oldsched-variants.json).
