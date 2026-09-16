# Step 7.6f1: external re-take at fair layouts (Anvil)

*2026-09-15. Checkpoint 2's prerequisite. Every system chooses its own
process layout on equal nodes; setup and protocol are in
[benchmarks/README.md](../benchmarks/README.md#external-re-take-on-anvil-step-76f1).*

## What was run

Jobs 20750616 (1 node) and 20750617 (2 nodes), `run.py --mode external
--workers 120 --timeout 180 --sources 4 --reps 1`, on orkut, mesh24, road-usa
and rmat25: one graph per class, sized so that compute exceeds setup
([checkpoint 1](step76-policy.md#large-inputs-checkpoint-1)). Smoke gates
20750465/66 first matched every layout's digest on road-ny, rmat20 and mesh20.

- **Builds.** RIKEN, GAPBS and Gluon are at the 7.5 revisions, with one
  toolchain for every system (GCC 11.2, OpenMPI 4.0.6/UCX). ACIC is the
  7.6f2 binary (`657ff378...`).
- **Search.** Each system chooses a layout on one tuning source, then its
  parameters on both.
- **Test.** Four held-out sources, each run once, in random order.
- **Outcome.** 136 timed runs, none hung, wrong or crashed; ACIC `control`
  floors 1.06x and 1.04x.
- **Cost.** 35 minutes per job, about 225 SU.
- **RIKEN on road-usa.** Not run: its distances exceed RIKEN's exact binary32
  range.

Selected layouts:

| System | 1 node | 2 nodes |
|---|---|---|
| ACIC | 8 x 15 (16 x 7 on road-usa) | 8 x 15 on orkut and rmat25; 16 x 7 on mesh24 and road-usa |
| RIKEN | 16 ranks/node on every graph, delta 16-64 | same |
| Gluon-Async | 1 rank/node, oec | 1 or 8 ranks/node, oec |
| GAPBS | 16-127 threads | — |

RIKEN chose the largest layout offered on every graph, at both node counts.
Its search hit the edge of the grid, so the RIKEN times below are an upper
bound on its best.

## Results

Median solve seconds over four held-out sources. In the ratio columns, above 1
the external system is faster than ACIC (paired median, ACIC / system).

| graph | nodes | ACIC | GAPBS | RIKEN | Gluon-Async | ACIC/GAPBS | ACIC/RIKEN | ACIC/Gluon |
|---|---|---|---|---|---|---|---|---|
| mesh24 | 1 | 1.37 | 0.19 | 26.4 | 4.39 | **7.2** | 1/18.3 | 1/3.2 |
| mesh24 | 2 | 0.84 | — | 17.1 | 3.98 | — | 1/20.6 | 1/4.9 |
| orkut | 1 | 0.67 | 0.16 | 0.10 | 0.87 | **4.3** | **6.3** | 1/1.25 |
| orkut | 2 | 0.65 | — | 0.065 | 1.55 | — | **9.7** | 1/2.6 |
| rmat25 | 1 | 2.09 | 1.19 | 0.33 | 1.55 | **1.8** | **6.6** | **1.35** |
| rmat25 | 2 | 1.36 | — | 0.20 | 2.11 | — | **6.9** | 1/1.45 |
| road-usa | 1 | 3.51 | 0.17 | n/a | 27.4 | **21.1** | — | 1/8.1 |
| road-usa | 2 | 3.87 | — | n/a | 37.7 | — | — | 1/9.6 |

Every pair in every cell falls on the same side (4/4), and every ratio is far
above both floors. Each cell comes from one allocation, but every ratio clears
the plan's 1.3x rule.

## What this says

1. **One-node GAPBS beats ACIC on every graph, including ACIC's 2-node runs.**
   It is 7x faster on mesh24 and 21x on road-usa. ACIC's best time on each
   graph stays behind GAPBS's one-node time by 1.1x (rmat25), 4x (orkut, mesh24)
   and 21x (road-usa). At these sizes ACIC uses more hardware than a one-node
   shared-memory code and is still slower.
2. **RIKEN beats ACIC 6-10x on the scale-free graphs (rmat25, orkut) and the
   gap widens at two nodes.** RIKEN loses 18-21x on mesh24. This is the 2024
   picture again, at a fair layout, and larger.
3. **ACIC beats Gluon-Async on high-diameter graphs (3-10x) and on orkut; they
   split rmat25.** This is the one distributed comparison ACIC wins cleanly.
4. **ACIC is not handicapped by its layout here.** It chose its own layout, and
   the 7.6f2 `tuned-fixed` times on these graphs do not change any row's
   direction: road-usa `tuned-fixed` 3.48 s at 2 nodes still trails GAPBS 21x.

[7.6i](step76-traces.md) traces rmat25 at two nodes and says where the loss to
RIKEN comes from: not latency, imbalance or idle time, but 318 ns of hardware
time per graph edge against RIKEN's 49, two thirds of it spent before an update
is sent.

Two qualifications, neither of which changes a row's direction:

- **Width map.** ACIC ran mesh24 and road-usa at the `log(V)` width because of
  the `PER_GRAPH_WIDTH_RULE` name-keyed map (see checkpoint 1).
- **Scale.** The inputs fit on one node, which favours GAPBS by construction. A
  distributed system earns its keep on graphs that do not fit, but ACIC has not
  shown that it scales past two nodes on these inputs (checkpoint 1).

## Against the plan's outcome reading

*SC27 as planned* requires "competitive with Gluon-Async/RIKEN at a fair
layout". This result meets that for Gluon-Async on high-diameter graphs and
for RIKEN on mesh24. It fails for RIKEN on scale-free graphs by 6-10x, and fails against a
single-node shared-memory baseline everywhere. Together with checkpoint 1
(no co-design, no scaling, adaptation losing to a fixed width on road-usa),
this matches the plan's **"stop the SC27 plan"** condition: *external systems
stay well ahead after a fair re-take.*

The narrow result that survives is ACIC against Gluon-Async on high-diameter
graphs at 1-2 nodes. That is an asynchronous-versus-asynchronous comparison
where GAPBS on one node is 7-21x faster than both, so it is not a
distributed-systems result on its own.

Not run, and what it would take to reopen the question:

- **Larger inputs.** Graphs that do not fit on one node, with ACIC scaling on
  them. Checkpoint 1 says it does not yet.
- **A wider RIKEN layout grid.** This could only make RIKEN faster.
- **Heaviest-edge width for adaptive on road-usa.** Closing the 7.6f2 gap to
  `tuned-fixed` would still leave road-usa 20x behind GAPBS.

`report_arms.py`'s header prints one process count per allocation, taken from
the last record; in this mode layouts differ per system, so ignore it. Report:
`campaign/report-external/arms.md`.

## 7.6n: re-take after 7.6k-m

*2026-09-16. Jobs 20770612 (1 node) and 20770615 (2 nodes), the same
command, graphs, sources and search as above. Only ACIC's binary changed:
`bin/acic` is now `a6ef533d...`, built on the pulled Reconverse tree with
Charm++ `--enable-shmem`, the destination-lookup table, and the 7.6k
defaults (buffer size from the controller, send filter by regime); see
[step76-klm.md](step76-klm.md). The 7.6f2 binary is kept as
`bin/acic_7.6f2`. The baselines' binaries are unchanged (same hashes).*

- **Outcome.** 342 runs. The only failures are the 8 runs of one RIKEN
  candidate, 16 ranks per node at delta 1024 on orkut, which crashed the
  same way in 7.6f1 and which the search drops. No hangs, no wrong digests.
- **Cost.** 34 and 36 minutes, about 225 SU.
- **Layouts.** ACIC chose 16 x 7 everywhere except orkut at 2 nodes (8 x 15).
  RIKEN again chose 16 ranks per node, the largest offered.
- **Floors.** Wider than 7.6f1's: 1.27x at 1 node, where ACIC's rmat25 runs
  are bimodal (the same configuration and source ran in 0.85 s and 1.32 s),
  and 1.11x at 2 nodes. The 1-node rmat25 cells are therefore not resolved.

Median solve seconds over the four held-out sources; 7.6f1's ACIC time in
parentheses. Ratios are paired medians (`campaign/report-external-76n`),
above 1 meaning the external system is faster.

| graph | nodes | ACIC (7.6f1) | GAPBS | RIKEN | Gluon-Async | ACIC/GAPBS | ACIC/RIKEN | ACIC/Gluon |
|---|---|---|---|---|---|---|---|---|
| mesh24 | 1 | 0.67 (1.37) | 0.19 | 26.5 | 4.49 | **3.4** | 1/37.7 | 1/7.2 |
| mesh24 | 2 | 0.66 (0.84) | — | 17.7 | 3.73 | — | 1/27.2 | 1/6.1 |
| orkut | 1 | 0.26 (0.67) | 0.16 | 0.106 | 0.85 | **1.7** | **2.4** | 1/3.1 |
| orkut | 2 | 0.23 (0.65) | — | 0.066 | 1.46 | — | **3.5** | 1/6.4 |
| rmat25 | 1 | 1.40 (2.09); control 0.92 | 1.16 | 0.32 | 1.79 | 1.2 · | **4.3** | 1/1.2 · |
| rmat25 | 2 | 0.64 (1.36) | — | 0.21 | 1.96 | — | **2.8** | 1/3.0 |
| road-usa | 1 | 1.86 (3.51) | 0.15 | n/a | 26.0 | **11.7** | — | 1/14.6 |
| road-usa | 2 | 1.57 (3.87) | — | n/a | 38.6 | — | — | 1/24.2 |

`·`: within the allocation floor.

What changed against 7.6f1:

1. **ACIC is 1.3-2.8x faster on every graph and node count**, and now beats
   Gluon-Async everywhere it was measured, including rmat25 at 2 nodes (3x,
   where 7.6f1 split). Every pair fell on ACIC's side except the unresolved
   1-node rmat25 cell.
2. **The RIKEN gap on scale-free graphs halved but is still 2.4-4.3x.**
   rmat25 went from 6.9x to 2.8x at 2 nodes, orkut from 9.7x to 3.5x. On
   rmat25 the gap narrows from 1 to 2 nodes (4.3x -> 2.8x), where 7.6f1's
   widened; on orkut it still widens (2.4x -> 3.5x).
3. **GAPBS on one node is still ahead of ACIC on every graph**, by 1.2x
   (rmat25, within the floor) to 12x (road-usa).

### Scaling entry condition

The [SC27 plan](sc27-plan.md#scaling-entry-condition) asks for three things
before any run above two nodes:

| condition | result | met? |
|---|---|---|
| ACIC within about 2x of RIKEN per graph edge on rmat25 and orkut | 2.8x and 3.5x at 2 nodes, on equal nodes; rmat25 is 136 ns per edge on ACIC's 224 workers against RIKEN's 49 | **no** |
| no regression on mesh24 or road-usa against 7.6f1 | 1.3-2.5x faster | yes |
| a profile of ACIC's communication share | not taken on the shared-memory build | no |

The condition is missed. The plan says to stop if it is still missed after
7.6k-m; that decision is open ([plan](sc27-plan.md#status)).
