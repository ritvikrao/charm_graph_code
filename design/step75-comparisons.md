# Step 7.5: matched-input SSSP comparisons on Delta

*2026-09-13. Executed comparison pilot following the
[post-step-7 review](post-step7-review.md). Reproduction instructions are in
[benchmarks/README.md](../benchmarks/README.md); compact measurements and
provenance are in [step75-data](step75-data/).*

## Decision

**Do not begin the generalization refactor yet. Start step 7.6 with a progress
repair, then a bounded controller and local-execution study.** The comparisons
establish useful performance regions, but do not establish a general adaptive
advantage. This is a completed pilot, not the paper's final evaluation or a
claim to have compared every current state-of-the-art implementation.

Three results change the next work:

1. **Progress failures are a correctness gate.** Two-node mesh/current and
   full-node RMAT/tuned-fixed queries stopped making progress and timed out.
   Failed variants' timings are suppressed; successful diagnostic replays
   cannot erase the original failures.
2. **External competitiveness depends on graph structure.** At 16 workers per
   node, ACIC beats RIKEN without presolve in completed mesh cells, but loses on RMAT, uniform,
   road, and social inputs. GAPBS exposes a substantial single-node execution
   deficit, including on meshes. Gluon provides a distinct asynchronous
   comparator, with results paired against ACIC in its own allocations.
3. **Scaling and deployment affect the controller.** Large-mesh work rises from
   about 18 million delivered updates on one node to one billion on 16 nodes.
   Coarsening disappears at four nodes. On one physical node, changing ACIC
   from 1 × 120 to 8 × 15 workers improves the tested mesh and RMAT dramatically.
   Control-policy conclusions must account for these effects.

## Inputs, resources, and measurement

All implementations solve the **same canonical undirected, positive-integer
weighted graphs**, with identical source IDs and distance semantics. Synthetic
graphs are symmetrized, deduplicated by minimum weight, and stripped of
self-loops. They differ from the directed generated inputs in the earlier
step 7 tables; do not multiply these speedups by the earlier A/B gains.

<!-- BEGIN INPUT_TABLE -->
| Input | Vertices | Directed arcs stored | Maximum weight |
|---|---:|---:|---:|
| mesh20 | 1,048,576 | 4,190,208 | 999 |
| mesh22 | 4,194,304 | 16,769,024 | 1,000 |
| rmat20 | 1,048,576 | 31,401,328 | 1,000 |
| rmat20-s2 | 1,048,576 | 31,401,260 | 1,000 |
| rmat22 | 4,194,304 | 128,309,684 | 1,000 |
| road-ny | 264,346 | 730,100 | 36,946 |
| uniform20 | 1,048,576 | 33,528,004 | 1,000 |
| uniform20-s2 | 1,048,576 | 33,570,550 | 1,000 |
| youtube | 1,134,890 | 5,975,248 | 1,000 |
<!-- END INPUT_TABLE -->

`20` and `22` denote 2^20 and 2^22 vertices. RMAT and uniform use generator
seeds 1 and 2 where listed, with target average out-degree 16 before
canonicalization. Uniform degrees are sampled from 0–32. RMAT uses the
Graph500 quadrant probabilities with permuted labels and no per-level noise;
it is not the official Graph500 generator. NY retains actual distance weights from the
[DIMACS road dataset](https://www.diag.uniroma1.it/~challenge9/download.shtml).
[SNAP YouTube](https://snap.stanford.edu/data/com-Youtube.html) has real social
topology and deterministic synthetic weights; sorted original labels are
remapped to consecutive IDs. [graphs.json](step75-data/graphs.json) records
hashes, all ten source IDs, reachable sizes, and independent reference digests.

Delta CPU nodes have 128 physical AMD EPYC 7763 cores and 256 GB memory.
Measurement allocations are exclusive; “nodes” always means physical nodes.
The main matrix uses one ACIC SMP process per physical node with 16, 64, or
120 workers, with one core per process left free for the OS. **These runs also
passed `+commap`, which Reconverse does not parse: it has no communication
thread, so that free core carried no ACIC thread and was never meant to.** The
inert flag is gone from the harness and the core budget is unchanged, so these
numbers stand; “communication core” below and in earlier notes should be read
as “the core left to the OS”. RIKEN tunes the MPI/OpenMP split:
{1,4} ranks/node in the original 16-worker one/two-node pilot, {1,2,4} in the
larger scaling and occupancy cases. GAPBS uses OpenMP on one node only.
All comparisons have equal allocated node budgets; occupancy, the OS core per
ACIC process, and rank placement are specified in the harness and exact
commands.

Two deterministic nonzero-degree sources tune each graph/resource case.
Eight distinct sources are reserved for testing, each with two randomized
repetitions. RIKEN and GAPBS tune four delta values. Fixed ACIC tunes flush
interval {1,5} and three widths. Choices are frozen before the test sources.
This bounded search is not an oracle or a proof that a baseline is optimally
tuned. Warmups and every unsuccessful tuning attempt are retained.

Times below are **solve seconds**, excluding graph input/construction,
optional RIKEN presolve, and digest verification. Kernel boundaries follow
each implementation's entry point; GAPBS initializes its vectors inside its
timer, while ACIC creates per-vertex state with the graph. Every accepted
query exits successfully and matches an independent serial 64-bit Dijkstra
reference, through two 64-bit hashes plus reached count and distance sum.
These are strong probabilistic checks, not a scalable shortest-path
certificate. RIKEN's binary32 values use exact power-of-two weight scaling;
results must map back to exact integers below 2^24.

Reported means are geometric means of the two repeats within each source,
then geometric means across sources. Ratios are paired by source; 95% intervals
use 10,000 seeded bootstrap resamples of the eight sources. They quantify
source variation within the allocation, **not** allocation-to-allocation noise
or uncertainty across the population of graphs. Baseline/ACIC > 1 favors ACIC.

## Primary comparison table

RIKEN here has **presolve disabled**. “Old fixed” reproduces pre-step-7 policy
settings in the current repaired binary; it is not a rebuilt historical code
snapshot. “Relaxed admission” sets both percentiles to one but retains ACIC's
window, queues, and controller. GAPBS entries are absent on multiple nodes.

<!-- BEGIN PRIMARY_TABLE -->
| Nodes | Workers/node | Graph | Sources | ACIC current (s) | Old fixed (s) | Tuned fixed (s) | Relaxed admission (s) | RIKEN (s) | GAPBS (s) | RIKEN / ACIC [95% CI] |
|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 16 | mesh20 | 8 | 0.3433 | 0.6202 | 0.3173 | 0.4093 | 1.9572 | 0.0251 | 5.70 [5.08, 6.24] |
| 1 | 16 | mesh22 | 8 | 0.9744 | 1.2597 | 0.9164 | 1.0307 | 9.9722 | 0.0892 | 10.23 [8.89, 11.88] |
| 1 | 16 | rmat20 | 8 | 0.2712 | 0.2825 | 0.2758 | 0.2872 | 0.0557 | 0.0730 | 0.21 [0.20, 0.21] |
| 1 | 16 | rmat20-s2 | 8 | 0.2781 | 0.2950 | 0.3007 | 0.2826 | 0.0568 | 0.0772 | 0.20 [0.19, 0.22] |
| 1 | 16 | rmat22 | 8 | 1.0754 | 1.0634 | 1.1058 | 1.0764 | 0.2041 | 0.2167 | 0.19 [0.18, 0.19] |
| 1 | 16 | road-ny | 8 | 0.1883 | 0.4109 | 0.1254 | 0.1881 | 0.1052 | 0.0061 | 0.56 [0.53, 0.59] |
| 1 | 16 | uniform20 | 8 | 0.2950 | 0.2894 | 0.2874 | 0.2877 | 0.1192 | 0.0643 | 0.40 [0.39, 0.42] |
| 1 | 16 | uniform20-s2 | 8 | 0.2891 | 0.2939 | 0.2875 | 0.2906 | 0.1227 | 0.0670 | 0.42 [0.42, 0.43] |
| 1 | 16 | youtube | 8 | 0.1731 | 0.1799 | 0.1844 | 0.1614 | 0.0548 | 0.0243 | 0.32 [0.30, 0.33] |
| 1 | 64 | mesh22 | 8 | 2.4734 | 2.5407 | 1.8238 | 2.4482 | 6.4746 | 0.0530 | 2.62 [2.17, 3.11] |
| 1 | 64 | rmat22 | 8 | 0.9500 | 1.0057 | 0.9534 | 1.0456 | 0.1733 | 0.1830 | 0.18 [0.16, 0.20] |
| 1 | 64 | road-ny | 8 | 0.6140 | 1.9118 | 0.5489 | 0.6158 | 0.1363 | 0.0092 | 0.22 [0.21, 0.24] |
| 1 | 64 | youtube | 8 | 0.1420 | 0.2754 | 0.1099 | 0.0962 | 0.0457 | 0.0329 | 0.32 [0.27, 0.38] |
| 1 | 120 | mesh22 | 8 | 5.5653 | 8.3527 | 4.6778 | 5.7175 | 6.1546 | 0.1044 | 1.11 [0.83, 1.42] |
| 1 | 120 | rmat22 | 8 | 4.3940 | 2.3832 | FAIL (4/16) | 4.6931 | 0.1752 | 0.3037 | 0.04 [0.03, 0.06] |
| 2 | 16 | mesh20 | 8 | FAIL (1/16) | 0.7509 | 0.3626 | 0.4914 | 1.2879 | — | — |
| 2 | 16 | mesh22 | 8 | 0.8922 | 1.2867 | 0.8165 | 1.0715 | 6.0904 | — | 6.83 [6.23, 7.60] |
| 2 | 16 | rmat20 | 8 | 0.1713 | 0.1865 | 0.1681 | 0.1757 | 0.0372 | — | 0.22 [0.21, 0.22] |
| 2 | 16 | rmat20-s2 | 8 | 0.1829 | 0.1941 | 0.1756 | 0.1795 | 0.0361 | — | 0.20 [0.19, 0.21] |
| 2 | 16 | rmat22 | 8 | 0.6626 | 0.6590 | 0.6400 | 0.6698 | 0.0877 | — | 0.13 [0.12, 0.14] |
| 2 | 16 | road-ny | 8 | 0.2142 | 0.5757 | 0.1420 | 0.2145 | 0.1205 | — | 0.56 [0.54, 0.58] |
| 2 | 16 | uniform20 | 8 | 0.1666 | 0.1806 | 0.1676 | 0.1653 | 0.0720 | — | 0.43 [0.42, 0.45] |
| 2 | 16 | uniform20-s2 | 8 | 0.1713 | 0.1862 | 0.1689 | 0.1744 | 0.0793 | — | 0.46 [0.44, 0.48] |
| 2 | 16 | youtube | 8 | 0.1216 | 0.1319 | 0.1117 | 0.1003 | 0.0317 | — | 0.26 [0.24, 0.28] |
| 4 | 16 | mesh22 | 8 | 1.0471 | 1.3186 | 0.7704 | 1.0769 | 4.6200 | — | 4.41 [3.92, 4.98] |
| 4 | 16 | rmat22 | 8 | 0.3473 | 0.3831 | 0.3540 | 0.3392 | 0.0575 | — | 0.17 [0.16, 0.18] |
| 8 | 16 | mesh22 | 8 | 0.9221 | 1.6273 | 0.7420 | 0.9249 | 3.1336 | — | 3.40 [3.11, 3.69] |
| 8 | 16 | rmat22 | 8 | 0.2096 | 0.2293 | 0.2127 | 0.2041 | 0.0455 | — | 0.22 [0.21, 0.23] |
| 16 | 16 | mesh22 | 8 | 0.8641 | 2.2155 | 0.9483 | 0.8759 | 2.4375 | — | 2.82 [2.54, 3.08] |
| 16 | 16 | rmat22 | 8 | 0.1692 | 0.2074 | 0.1471 | 0.1442 | 0.0356 | — | 0.21 [0.19, 0.24] |
<!-- END PRIMARY_TABLE -->

The failed two-node mesh/current cell includes 15 successful queries and one
timeout out of 16 attempts. No successful-only mean or speedup is published
for that cell. The last unattempted relaxed-admission query was completed in
job 22033692 using the original frozen settings; all its other observations
come from job 22032689. That supplemental allocation is explicitly retained.
The 1 × 120 RMAT/tuned-fixed cell has four timeouts out of 16 attempts:
sources 2631403 and 3882232 both fail in both repetitions. Its
unattempted slots are completed using frozen choices in job 22034066, in
randomized order, retaining the original failure. That occupancy row spans
two allocations; its intervals do not quantify allocation effects. The
separate full-node layout table has its own paired allocation.

The 64- and 120-worker rows describe the initial **single-process** deployment.
Use the paired layout follow-up below to assess a practical full-node ACIC
configuration. These fixed-size inputs are a strong-scaling pilot; 16 workers
per node is not a full-node efficiency result and there is no weak-scaling claim.

## Gluon-Async and Gluon-Sync

The pinned [Galois distributed SSSP implementation](https://github.com/IntelligentSoftwareSystems/Galois)
is run in both Async and Sync modes. Priorities {0, denominator/16, denominator}
and, on two nodes, outgoing edge cut versus Cartesian vertex cut are tuned on
the two training sources. ACIC and its duplicate run in the same allocations
as Gluon; do not pair these Gluon times with ACIC times from the primary table.

<!-- BEGIN GLUON_TABLE -->
| Nodes | Workers/node | Graph | ACIC (s) | Gluon-Async (s) | Gluon-Sync (s) | Async / ACIC [95% CI] | Sync / ACIC [95% CI] | Control / ACIC [95% CI] |
|---:|---:|---|---:|---:|---:|---|---|---|
| 1 | 16 | mesh20 | 0.2313 | 0.4118 | 0.4365 | 1.78 [1.22, 2.55] | 1.89 [1.30, 2.66] | 1.02 [0.98, 1.07] |
| 1 | 16 | rmat20 | 0.1685 | 0.1077 | 0.1050 | 0.64 [0.57, 0.70] | 0.62 [0.56, 0.68] | 1.02 [1.00, 1.05] |
| 1 | 16 | road-ny | 0.1169 | 0.0806 | 0.0884 | 0.69 [0.59, 0.81] | 0.76 [0.66, 0.87] | 1.00 [0.98, 1.02] |
| 1 | 16 | youtube | 0.1096 | 0.0499 | 0.0560 | 0.46 [0.41, 0.50] | 0.51 [0.47, 0.55] | 1.01 [0.98, 1.05] |
| 2 | 16 | mesh20 | 0.4223 | 0.6978 | 19.2656 | 1.65 [1.15, 2.32] | 45.63 [31.77, 62.22] | 1.02 [0.87, 1.14] |
| 2 | 16 | rmat20 | 0.1845 | 0.3059 | 0.3926 | 1.66 [1.47, 1.85] | 2.13 [1.90, 2.30] | 0.95 [0.84, 1.03] |
| 2 | 16 | road-ny | 0.2230 | 0.2595 | 7.9237 | 1.16 [1.03, 1.31] | 35.53 [32.95, 38.36] | 0.97 [0.94, 1.00] |
| 2 | 16 | youtube | 0.1177 | 0.2585 | 0.3799 | 2.20 [2.03, 2.37] | 3.23 [3.03, 3.44] | 1.01 [0.97, 1.05] |
<!-- END GLUON_TABLE -->

This separate pairing matters: ACIC itself varies appreciably across
allocations. The timer/checksum adapter leaves Gluon's relaxation, scheduling,
partitioning, and termination kernels unchanged. Failed adapter attempts are
listed below and excluded from accepted solver timings.

ACIC is 1.16–2.20× faster than Gluon-Async in these two-node cells, while
Gluon-Async wins three of the four one-node cases. That is a useful distributed
result alongside the persistent RIKEN deficit; it does not establish a universal
winner. Gluon uses one MPI rank per node in this pilot. Broader rank/layout
tuning and larger graphs are needed before making a strong general comparison.

## Full-node process layout

The layout probe keeps 120 ACIC workers on one physical node and compares
1 × 120, 4 × 30, and 8 × 15. Each process gets a contiguous core region one
core short of its stride, leaving that core to the OS, so the 8 × 15 layout
places one process per 16-core NUMA domain and runs 120 worker threads on 120
of the 128 cores. Fixed here means flush every round, width 1024, no bucket adaptation
or idle flush; it is a specified diagnostic policy, not a per-layout tuned winner.

<!-- BEGIN NUMA_TABLE -->
| Physical nodes | Graph | Policy | 1 × 120 (s) | 4 × 30 (s) | 8 × 15 (s) | 1 × 120 / 4 × 30 [95% CI] | 1 × 120 / 8 × 15 [95% CI] |
|---:|---|---|---:|---:|---:|---|---|
| 1 | mesh22 | current | 5.5560 | 1.1515 | 0.8016 | 4.82 [3.92, 5.78] | 6.93 [5.71, 8.20] |
| 1 | mesh22 | fixed | 7.8463 | 1.1980 | 0.6483 | 6.55 [6.29, 6.83] | 12.10 [11.14, 13.28] |
| 1 | rmat22 | current | 4.4883 | 0.3703 | 0.3022 | 12.12 [7.50, 18.30] | 14.85 [11.42, 19.14] |
| 1 | rmat22 | fixed | 2.3297 | 0.3067 | 0.2866 | 7.60 [7.17, 8.07] | 8.13 [6.53, 9.57] |
<!-- END NUMA_TABLE -->

This changes process geometry, communication endpoints, and the controller's
`CkNumNodes()`-dependent starvation threshold as well as placement. It is not
an isolated NUMA or affinity experiment. The PE count remains 120, so the
`N * 100` coarsening-eligibility threshold itself is constant in this probe.

The following independent allocation pairs the selected 8 × 15 layout with
the RIKEN/GAPBS configurations frozen in the original 120-worker tuning job.
The fixed ACIC setting is transferred from that job without retuning. Layout
selection used the geometry probe above on these same sources: this confirms
a deployment choice, not a new held-out generalization claim.

<!-- BEGIN LAYOUT_TABLE -->
| Physical nodes | Graph | ACIC 8 × 15 (s) | Transferred fixed 8 × 15 (s) | RIKEN (s) | GAPBS (s) | RIKEN / ACIC [95% CI] | GAPBS / ACIC [95% CI] | Control / ACIC [95% CI] |
|---:|---|---:|---:|---:|---:|---|---|---|
| 1 | mesh22 | 0.9287 | 1.1707 | 6.4956 | 0.0917 | 6.99 [5.79, 8.28] | 0.10 [0.09, 0.11] | 1.00 [0.98, 1.01] |
| 1 | rmat22 | 0.2777 | FAIL | 0.1785 | 0.2805 | 0.64 [0.55, 0.72] | 1.01 [0.82, 1.25] | 0.98 [0.90, 1.06] |
<!-- END LAYOUT_TABLE -->

The transferred narrow-width RMAT fixed policy stalled again in job 22033887
(source 3882232), this time after processing just three updates. Its remaining
follow-up slots are explicitly marked unrun after validation failure. Job
22034120 completes the other configurations and the mesh case using the
frozen choices. Failed fixed-policy timings are suppressed; the RMAT follow-up
spans these two allocations and its intervals do not model allocation effects.
This fixes occupancy at 120 workers per physical node. It does not select
each system's best thread count independently; for example, the primary
GAPBS occupancy rows show that more threads need not be faster. A later
equal-node-budget evaluation should tune occupancy as well as rank layout
on separate training cases, then freeze both.

## Adaptivity, control noise, and work

### Timed console output

The production build defines `INFO_PRINTS` and prints on every controller
round during the solve. The primary table preserves that existing behavior.
A separate one-node, 16-worker allocation compares it against a copy built
with only that definition removed; the application source and primary binary
are preserved. Frozen RIKEN/GAPBS choices run alongside both ACIC builds.

<!-- BEGIN QUIET_TABLE -->
| Graph | Default ACIC (s) | Quiet ACIC (s) | RIKEN (s) | GAPBS (s) | Default / quiet [95% CI] | RIKEN / quiet [95% CI] | GAPBS / quiet [95% CI] |
|---|---:|---:|---:|---:|---|---|---|
| mesh20 | 0.3599 | 0.3352 | 1.9005 | 0.0237 | 1.07 [1.04, 1.11] | 5.67 [5.08, 6.29] | 0.07 [0.06, 0.08] |
| mesh22 | 0.9833 | 0.9194 | 9.9993 | 0.0862 | 1.07 [1.03, 1.11] | 10.88 [9.34, 12.96] | 0.09 [0.08, 0.10] |
| rmat22 | 1.0710 | 1.0975 | 0.2047 | 0.2198 | 0.98 [0.94, 1.00] | 0.19 [0.18, 0.19] | 0.20 [0.19, 0.21] |
| road-ny | 0.1804 | 0.1814 | 0.1076 | 0.0063 | 0.99 [0.96, 1.03] | 0.59 [0.56, 0.63] | 0.03 [0.03, 0.04] |
<!-- END QUIET_TABLE -->

This measures output sensitivity, including changes in asynchronous scheduling
and work caused by removing printing. It is not a constant cost that can be
subtracted from other allocations, and quiet timings must not be substituted
into the primary or scaling tables. Use quiet production timing with bounded
diagnostic output in the next campaign.
Removing printing improves the two mesh cases by about 7%, with little
benefit on RMAT or NY. It does not explain the main external deficits.

### Policy controls

The complete [control table](step75-data/controls.md) compares identical
duplicate runs, tuned fixed, and old fixed against current defaults. Improvements
against the old defaults do not establish a need for adaptation. The tuned
fixed policy is often as fast or faster; on four-node mesh22, it takes about
0.77 s versus 1.05 s for current. A single global fixed configuration and an
interaction ablation remain necessary for Gate A.

The original two-node RMAT20 duplicate was about 15% slower than its identical
current counterpart. The following independent allocation repeats the frozen
settings, checks a square four-rank RIKEN grid, and retains a fresh duplicate:

<!-- BEGIN CONFIRMATION_TABLE -->
| Job | Nodes | Graph | ACIC (s) | Tuned fixed (s) | Frozen RIKEN (s) | Retuned RIKEN (s) | Control / ACIC [95% CI] |
|---|---:|---|---:|---:|---:|---:|---|
| 22032818 | 2 | rmat20 | 0.1791 | 0.1716 | 0.0346 | 0.0347 | 1.01 [0.92, 1.10] |
<!-- END CONFIRMATION_TABLE -->

The additional RIKEN layout did not improve the selected setting. The external
RMAT deficit persists; small internal improvements require more allocation
replication before being treated as robust.

Mesh22 counters provide a concrete next hypothesis. Entries are medians of
completed queries, not edge-examination counts. “Delivered updates” is the
application's `updates_noted` counter, incremented on update arrivals including
rejections. TRAM bytes, retained in [counters.csv](step75-data/counters.csv),
are payload accounting rather than observed wire traffic.

<!-- BEGIN COUNTER_TABLE -->
| Nodes × 16 workers | Current: delivered updates (M) | Fixed: delivered updates (M) | Current reductions | Fixed reductions | Current coarsenings |
|---:|---:|---:|---:|---:|---:|
| 1 | 18.13 | 20.77 | 7,706.0 | 5,852.0 | 2 |
| 2 | 20.52 | 22.46 | 5,605.5 | 5,313.5 | 2 |
| 4 | 279.81 | 25.41 | 825.5 | 4,929.0 | 0 |
| 8 | 537.89 | 37.54 | 748.5 | 3,955.5 | 0 |
| 16 | 1000.32 | 61.20 | 723.5 | 3,788.5 | 0 |
<!-- END COUNTER_TABLE -->

At 16 nodes, current is about 9% faster than tuned fixed while delivering
about 16 times as many updates. Thus the extra work is not automatically the
dominant cost: fewer controller rounds can offset it. The next experiment
must optimize time while explaining work, rather than treating minimum work
as the objective.

`choose_coarsening` declines to act in the `histogram_sum <= N * 100` branch,
where N is the total PE count, and whenever clamped mass is present. File-mode
initial width is log2(V), whereas the generated mesh mode uses sqrt(V).
The fixed search can avoid this narrow initial range. **The counters support
investigating these interactions, but do not prove which guard causes the
work explosion.** A round-level eligibility/clamping study is needed; removing
the clamp guard without a representation argument could break correctness.

## Preprocessing and timing limits

This two-node, two-training-source sensitivity probe compares identical
RIKEN rank/delta settings with and without 4000 requested presolve rounds,
bounded by upstream `PRESOL_SECONDS=30`. The limit is checked at round
boundaries, so actual preprocessing exceeds 30 seconds slightly. It is not
the uncapped upstream default or an optimized presolve configuration.

<!-- BEGIN PRESOLVE_TABLE -->
| Graph | Sources | RIKEN no presolve (s) | RIKEN with presolve (s) | Added presolve time (s) | Solve speedup | Approx. queries to amortize |
|---|---:|---:|---:|---:|---:|---:|
| mesh20 | 2 | 1.2130 | 0.7567 | 31.02 | 1.60 | 68 |
| rmat20 | 2 | 0.0926 | 0.0524 | 31.03 | 1.77 | 772 |
| road-ny | 2 | 0.1488 | 0.0938 | 31.03 | 1.59 | 565 |
<!-- END PRESOLVE_TABLE -->

Amortization divides mean added presolve time by the difference of geometric
mean solve times. It is only a rough repeated-query estimate for these two
sources and fixed settings, excluding graph-construction cost.

This pilot makes **no end-to-end or memory-superiority claim**. ACIC's printed
`Read time` is unassigned for binary file mode and must not be used. Its `Total
time` includes setup/statistics; launcher wall time additionally includes Slurm
step startup. RIKEN's construction timer covers rank zero's constructor, not
the adapter's input or all preparation. High-occupancy RIKEN construction can
take tens of seconds even when its kernel takes well below a second. A later
evaluation needs matched cold-start and reused-graph boundaries. Baseline RSS
printed by the adapters is rank zero/process RSS, while ACIC graph bytes omit
runtime reservations; these cannot be compared as aggregate peak memory.

## Validation and failure ledger

The only production algorithm change for this campaign removes the minimum
1001-created-update termination guard while retaining the unchanged two-round
accounting condition; the source contributes the extra processed update.
`--result-digest` computes the parallel digest after the timer without serial
verification inside every distributed process. Timeouts return failure even
when a partial digest is printed. No controller or performance policy was
optimized during the primary campaign.

- The existing verification script passed 18 normal and three diagnostic
  configurations (job 22032651). Independent Python Dijkstra fixtures covering
  empty graphs, an isolated source, a small disconnected component, and a
  permuted cross-partition path passed 16 cases each on one and two nodes
  (22032608, 22032690), including delayed rounds and `--verify`.
- The initial SNAP preparation assumed consecutive labels and failed
  (22032511); canonical relabeling repaired it before preparation/reference
  job 22032565. No rejected graph was used as a timing input.
- The initial smoke timing parser could match `presolve_seconds=0`; the parser
  was anchored to `BENCH ... solve_seconds` before the primary jobs. Smoke
  job 22032609 establishes digest/execution checks only, not performance.
- Some large-delta RIKEN candidates exceeded the upstream node-send buffer
  limit. They are retained as invalid tuning attempts and cannot be selected.
  This restricts the measured tuning envelope; it is not an ACIC speedup.
- Two initial Gluon adapter jobs (22033282/22033283) omitted reset of upstream
  accumulator caches and failed the digest gate. Correcting the checksum
  adapter produced valid one-node results (22033417). Two-node job 22033418
  then produced correct distances but exited with SIGSEGV after statistics;
  none of those timings were accepted. GDB job 22033720 identifies the unused
  Cray LibSci shutdown/profiling path in `getenv`; relinking without that
  library preserves the solver and resolves the observed teardown failure.
  The first debugger attempt (22033674) was cancelled after startup stalled;
  the replacement disabled GDB startup files, debuginfod, and worker threads.
- **ACIC mesh20, two nodes, source 736504, repetition 1:** job 22032689 query
  1133 stopped changing its progress state at about 0.540 s, then hit the
  120 s timeout. It had 605,016 created, 605,017 noted, and 601,703 processed
  updates; heap/TRAM thresholds stayed 102/125. It ran about 1.83 million
  reductions without convergence. Its partial distances failed the reference
  and the launch returned nonzero. The failure remains unresolved in this
  measured binary. Five successful mesh replays are evidence of intermittency,
  not evidence of correctness.
- **ACIC rmat22, 1 × 120 workers, source 3882232, repetition 0, tuned-fixed:**
  job 22033411 query 252 timed out with flush interval 1, bucket width 3,
  bucket adaptation and idle flush disabled. It reported 246,571,826 created,
  246,571,827 noted, and 246,100,381 processed updates, with an empty reduced
  window (`first_nonzero=-1`) despite pending work. Its partial digest failed.
  This demonstrates that the progress issue is not confined to the adaptive
  policy. Job 22034066 completes missing slots and replays this case alongside
  current defaults; replays remain separate from the primary estimates.
  The fixed policy fails all five replays, while current passes all five.
  Source 2631403 also times out in both primary repetitions under that fixed
  policy. These repeated failures are stronger evidence than the intermittent
  default-policy mesh failure, and must be addressed independently if needed.
- The same narrow fixed policy failed in the 8 × 15 layout follow-up
  (22033887 query 12), with 25,649 created, 25,650 noted, and only three
  processed updates despite an empty reduced window. The remaining slots for
  that failed configuration are recorded as **skipped**, without timings.
  This reproduces a progress failure under a second process geometry.

<!-- BEGIN REPLAY_TABLE -->
| Job | Graph | Source | Configuration | Valid / attempted | Successful-query geomean (s), diagnostic only |
|---|---|---:|---|---:|---:|
| 22033692 | mesh20 | 736504 | current | 5/5 | 0.2965 |
| 22033692 | mesh20 | 736504 | tuned-fixed | 5/5 | 0.3437 |
| 22034066 | rmat22 | 3882232 | current | 5/5 | 2.4561 |
| 22034066 | rmat22 | 3882232 | tuned-fixed | 0/5 | — |
<!-- END REPLAY_TABLE -->

## Next work and stopping rules

1. **Progress first (about 2–3 working days before performance changes).**
   Reproduce the recorded mesh and RMAT sources under their failing policies
   with repeated allocations and delayed delivery. Capture bounded diagnostics
   around loss of progress: window
   origin, pending-work minima/maxima, clamped mass, held/PQ counts, and the
   update conservation invariant. Add a minimized regression and a reasoned
   progress argument for the repair. Keep both this failure and the original
   performance snapshot. A watchdog that forces an exit is not a repair.
   Include a priority-gap fixture: width 3 gives a 256-bucket reduced window
   spanning only 768 distance units, smaller than this input's largest edge
   weight (1000). This is a hypothesis to test, not a diagnosed root cause.
2. **Controller robustness (one bounded experiment).** On canonical mesh20/22,
   independently vary initial width, the PE-dependent two-tier eligibility
   rule, and safe treatment of clamping; freeze process geometry. Record why
   each round can or cannot coarsen. Compare time and delivered work against
   current and a strong fixed policy at 1/2/4/8/16 nodes. Include RMAT and real
   graphs as regression controls. Do not conflate reduced rounds with reduced
   work or better time.
3. **Local execution and deployment.** Use the demonstrated 8 × 15 layout as a
   candidate, then separate process count, affinity, transport endpoints, and
   starvation policy. Profile a small representative subset against GAPBS;
   account for queue work, cache/memory behavior, aggregation, and exposed
   communication. Repair setup timing before any cold-start comparison.
   ACIC stores 64-bit destination IDs and weights in 16-byte edges; GAPBS uses
   32-bit fields for these inputs. Include representation width in the local
   profile before considering a packing change, retaining valid distance range.
   Audit the recorded LCI warning that the UCX registration cache is unavailable,
   then quantify any runtime configuration change in a matched comparison;
   do not assume that warning explains the observed gap.
   Wasp remains an additional local scheduling comparison, not a prerequisite
   for fixing the observed stall.
4. **Return to the research claim.** Test admission × delivery policies against
   one global fixed setting tuned on separate training graphs, plus the
   per-case tuned references. Repeat meaningful wins and regressions across
   allocations with frozen constants, and include an additional genuinely
   weighted input. Keep RIKEN presolve as a separate reused-graph comparison.
   Stop after one or two supported optimizations; if no adaptive advantage
   survives, narrow the claim before extracting a controller around BFS.

Step 7.5's comparison deliverable is complete. Its general correctness gate
and **Gate A remain open**. Steps 8–12 stay conditional; neither a framework
refactor nor more combining experiments addresses the findings above.

## Reproduction and storage

<!-- BEGIN PROVENANCE_SUMMARY -->
The primary matrix contains **30 graph/resource cells,
3,120 attempted test queries (3,115 valid)**,
plus 1,186 supplemental test attempts
(1,185 valid; 14
additional planned slots explicitly skipped after validation failure). Tuning, warmups,
presolve probes, failed adapters, and diagnostic replays are additional records.
See [provenance.md](step75-data/provenance.md) for pinned revisions and binary
identities, [job-accounting.psv](step75-data/job-accounting.psv) for Slurm states,
and [scratch-log-manifest.json](step75-data/scratch-log-manifest.json) for raw-log
locations and hashes. The initial build manifest predates the first commit;
the measured solver source is verified against commit `f29245e` by SHA-256.
<!-- END PROVENANCE_SUMMARY -->

The full raw solver output and graph/binary snapshots stay under
`/scratch/mzu/rao1/acic-comparison-20260913` (the work HDD alias resolves to the
same storage). The dependencies reuse the installed runtime and libraries;
there is one compact checkout/build per external implementation. No large
graph, binary, or raw per-query log is committed. The artifact preserves
compressed structured records, all selected configurations, graph and source
hashes, job outputs/accounting, and hashes locating the full scratch logs.
