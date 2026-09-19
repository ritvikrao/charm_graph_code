# One-node plan implementation on Delta

Starting revision: `db8a649`. Campaign: `/u/rao1/.tmp/ipdps27-onenode`.
CPU: AMD EPYC 7763, 128 physical cores, eight NUMA domains. Use Delta's
`cpu` partition, `mzu-delta-cpu` account, Cray Shasta PMI, hybrid CXI matching,
and `benchmarks/launch_acic.sh` for disjoint worker pinning. Default test
layout is eight processes of 15 workers per node; report the layout with
every result (the earlier 896-PE table used a different layout).

## D0 instrumentation

`scripts/delta/build_onenode.sh` builds private application/htram snapshots.
`acic_frozen` is rebuilt from `db8a649`, not the preexisting local executable.
`acic_ipdps_diag` enables communication timers, structural counters, and a
creator-PE tag carried through compact transport and deferred updates. The
tag reserves one diagnostic-only wire bit, limiting that build to vertex IDs
below 2^30; production's wire format is unchanged. Same-PE changes include
the single source injection. Reductions record how many PEs retired work
since their preceding contribution, with elapsed round time and thresholds.
The stale diagnostic expectation of -1 in the source bucket is corrected:
the solver already charges the source, so all final buckets must be zero.

GAPBS `--diag` uses upstream's existing iteration timer and preserves bucket
fusion. It buffers per-bucket records instead of printing inside each round.
Diagnostic timing is for diagnosis, not adoption A/B comparisons.
`benchmarks/onenode_report.py` reports time-weighted inactive participation,
changes per vertex by origin, measured work/send/other shares, and work per
change. These overlapping quantities are not added into a false wall-time
decomposition.

Validation: Delta job **22217886**, node cn033: existing gate passed
(18 configurations, three diagnostic configurations, 28 progress fixtures).
Six additional D0 runs (uniform, mesh, RMAT, at one/four PEs) passed serial
Dijkstra, zero live histogram, zero admitted drift, and provenance totals.
All binaries and source hashes are in the campaign manifests.

Paper-graph D0 measurements are pending graph preparation. The local older
campaign supplies mesh24; mesh26 is generated deterministically. Native USA
roads and coordinates come from the official DIMACS Challenge 9 dataset.
The NumPy/SciPy tools require `PYTHONNOUSERSITE=1` on this account, because
the user NumPy 2 installation is incompatible with the system SciPy build.

No optimization has been adopted as a default. L1 reader placement was
subsequently implemented after the pilot below; L4 remains conditional on
post-lever diagnosis.

## L1 relabeling experiment

`reorder_graph.py --tile T --pes P` deals compact tiles round-robin,
concatenates each owner's tiles, remaps physical sources, and checks independent
Dijkstra distance aggregates. `ordered` accepts an already Morton-ordered input.
Cut reporting now reproduces the existing reader's equal-edge partition loop;
assuming equal vertex ranges was inaccurate. Those edge-balanced boundaries may
split the intended tile-owner groups, so the report explicitly records this.
The initial experiment left the solver's reader unchanged; the later optional
reader implementation is described below.

Validation: three unit tests cover incomplete tiles, more PEs than vertices,
invalid sizes, and the exact reader partition on random/empty degree sequences.
A 256-vertex weighted mesh with T=7, P=4 preserved all six independently solved
source distance aggregates after relabeling. Preparation job 22218017 generates
the mesh24 sweep for 1/2/8 nodes and 1/4/16/64 pieces per PE.
`onenode_ab.py` interleaves variants, validates all four digest fields, pairs
physical held-out sources by reference row, retains warmups separately, and
fails on missing results or stalls. Include a repeated baseline arm to measure
the allocation floor. No timing result or adoption claim is available yet.


## D0 first allocations

Jobs 22218062 (one node) and 22218063 (two nodes), eight processes x 15
workers per physical node. All graph/source digests passed. Diagnostic results:

| graph | nodes | round-inactive PE-time | changes/V | cross-PE changes/V | work share |
|---|---:|---:|---:|---:|---:|
| mesh24-z | 1 | 74.8% | 31.03 | 0.24 | 14.4% |
| mesh26-z | 1 | 67.4% | 46.49 | 0.21 | 19.8% |
| road-usa-z | 1 | 78.6% | 28.58 | 0.08 | 10.4% |
| mesh24-z | 2 | 78.0% | 28.14 | 0.28 | 11.3% |
| mesh26-z | 2 | 70.0% | 39.70 | 0.23 | 16.4% |
| road-usa-z | 2 | 78.7% | 33.01 | 0.13 | 9.3% |

These structural results support keeping the proposed lever order: wavefront
concentration and same-PE re-work dominate. Diagnostic solve times differ
substantially from the earlier machine's production table; no performance
claim compares those unlike builds. Eight-node job 22218064 and tuned GAPBS
job 22218065 are tracked separately in the campaign.

## L2 implementation

`--process-share off|on|auto` (default off) publishes each PE's immutable CSR
and distance storage through the existing Charm++ nodegroup. Same-process
updates use an atomic minimum and enter per-worker bins, keyed by original
bucket index; peer workers steal admitted expansions. Edges between processes
retain htram. Every created update remains charged until a worker retires it,
including stale entries and updates delayed by a clamp-generation mismatch.
A worker expands a captured accepted distance, so concurrent improvements do
not change a scan halfway through. `auto` enables this path only below eight
arcs per vertex. Explicit sharing requires lazy-heavy off on dense graphs;
the current implementation does not share lazy edge-range tokens.

`--sources v1,v2,...` retains CSR across solves. A Charm++ quiescence boundary
precedes reset, followed by a reset reduction before the next injection.
Distance arrays, queues, ledgers, coarsening, filters, counters, and live
control state reset; control generations stay monotone. Old timeout callbacks
carry a source epoch. Each source gets its own timing, digest and diagnostic
files. `onenode_ab.py --batch` uses this path when all variants support it.

Validation so far: original gate with sharing on (job 22218082); genuine
two-node, two-process verification (22218153). The first two-node attempt
22218083 failed before solving because fixed OS core IDs fell outside its
shared allocation; correctness jobs now use their allocated CPU set.
Batch tests passed all 42 solves (22218181), covering repeated sources,
uniform/mesh/RMAT, both control paths, sharing on/off, overflow extension and
lazy tokens. Final bucketed build also passed batching (22218198) and
multi-node verification (22218197); the final full gate passed in job 22218196.
An eight-thread queue test checks exactly-once consumption of 80,000 items.
No performance adoption has been made.

## L3 implementation

`--slack-control off|on|auto` (default off) replaces percentile admission with
a frontier-relative slack, kept in original bucket widths across coarsening.
The controller samples successful changes per retired update and the fraction
of inactive PEs. A rising change ratio halves the slack; otherwise starvation
widens it by 1.5x. An EWMA, one-round cooldown, minimum sample size, and bounded
slack damp oscillation. Empty-window and overflow progress rules retain
precedence. Workers continue asynchronously within the admitted range.
Round CSVs include slack, input measurements, and the chosen action. State
resets for every source. Auto is restricted to sparse graphs pending evidence.

Validation: controller unit checks cover competing signals, cooldown, empty
samples, bounds, and coarsening. Full correctness gate passed with both sharing
and slack enabled (22218297), as did two-node verification (22218298) and all
42 batch-source solves (22218299). No default was changed. Paired performance
and scale-free checks remain required before adoption. L4 still requires
post-L1/L2/L3 diagnosis; no round-cost mechanism has been assumed necessary.


## Storage and boundary checks

The home directory has a 100 GiB soft quota. Tile preparation exhausted it;
its generated files were moved to
`/work/hdd/mzu/rao1/acic-ipdps27-onenode-20260919/graphs`, with compatibility
symlinks from the original campaign. Use the work campaign for further input
preparation; its bin/logs/build links retain the existing manifests and logs.
Mesh26 tile preparation resumed as job 22218452. Job 22218227 completed all
mesh24 pairs before failing during the next graph; its complete mesh24 result
is retained, and the remaining graphs are resubmitted as 22218463.

Review found a preexisting owner-table underflow for V < 1024. Clamping the
last table index to zero fixes it without changing large-graph lookup. Jobs
22218461/62 passed 60 solves each on one/two nodes: empty and disconnected
path graphs at 16, 256, 1023, 1024, 1025 vertices, sharing on/off, live slack,
and repeated sources spanning connected and isolated components.

First complete production result: mesh24-z, one node, job 22218227, four
held-out sources x three repetitions: L2/frozen median paired ratio **0.263**
(3.8x faster), worst source 0.288. Repeated frozen control median ratio 1.014.
This is one allocation, not an adoption or paper-pass claim. L3's first mesh24
allocations show no clear additional gain over L2, so its default remains off.

## L1 reader placement

The completed two-node mesh24 tuning pilot (22218279, two training sources,
two repetitions) gave paired ratios to frozen of 0.932, 0.670, 0.528, 0.438
for 1, 4, 16, 64 pieces per PE. The repeated baseline ratio was 0.868.
This justified implementing the optional reader path, not adopting a default.

`--reader-tile off|auto|T` defaults off. GAPBS input IDs are mapped once at
read time; source arguments and result digests retain their original IDs.
Contiguous tiles are dealt round-robin to processes with sharing enabled,
or to PEs otherwise. Owner-group boundaries are exact; within a shared process
its CSR is balanced by edge count among its PEs. Runtime updates still use
the existing destination table. Auto chooses 64 pieces per owner for sparse
inputs and stays off for dense inputs. This is a candidate policy awaiting
paired measurements, not a claim that 64 is globally optimal.

Validation: full existing gate 22218613 passed; jobs 22218615/16 passed 60
solves each with explicit T=7 on one node and auto on two nodes. These cover
empty partitions, uneven tile tails, fewer vertices than owners, isolated
sources, shared/off execution, live slack, and repeated-source reset. The
unit check exhaustively compares mapping/inverse/chunk boundaries against
explicit tile dealing for small and boundary-sized graphs. Independent scipy
relabeling tests passed using `PYTHONNOUSERSITE=1`.

Candidate binary `acic_reader_final` and diagnostic companion have immutable
source/hash manifests. Reader pilots 22218625/26 and regression pilot 22218627
use this exact build. Post-lever D0 jobs 22218629/30 measure the remaining
work, inactivity and round cost before deciding on L4.

## GAPBS baseline and outstanding acceptance work

Initial tuning 22218065 used the existing two-stage layout/parameter search.
Its optimum touched the smallest thread count (eight), and mesh24 touched
the largest delta. These results cannot establish the strongest baseline.
`onenode_gap_tune.py` therefore searches the joint thread/delta grid, including
1/2/4 threads, all 128 cores, and larger deltas; it confirms the top three
using training sources before measuring held-out sources. A second allocation
can reuse the selection with `--selection-job`. Job 22218622 runs this search.
Upstream bucket fusion remains intact. The initial bucket diagnostics completed
as 22218556: roughly 1,100–1,600 road buckets, 1,300–1,600 mesh24 buckets and
5,500–7,500 mesh26 buckets across the held-out sources. These use the initial
settings and will be replaced after stronger tuning.

The eight-node allocations are still queued. No eight-node/GAPBS pass, second
allocation confirmation, scale-free suite result, or default adoption is
claimed. The one-node L3 matrix exceeded its requested 30-minute window;
the scheduler refused a running-job time extension. Its mesh24 and mesh26
cells completed, and road was resubmitted as 22218658. The two-node L3 matrix
22218343 completed all three graphs in 27 minutes. Future matrix jobs request
an hour; complete graph cells remain usable after a later cell is interrupted.

Additional complete held-out results (four sources, three repetitions):

| job | graph | nodes | shared/frozen | slack/frozen | control/frozen |
|---|---|---:|---:|---:|---:|
| 22218342 | mesh26-z | 1 | 0.142 | 0.154 | 1.133 |
| 22218343 | mesh26-z | 2 | 0.180 | 0.185 | 0.909 |
| 22218343 | road-usa-z | 2 | 0.259 | 0.254 | 1.058 |

These are median paired ratios, not final acceptance results. There is no
consistent additional L3 gain. All new optimization defaults remain off.

`onenode_accept.py` requires two explicitly paired sets of four job IDs:
eight-node ACIC, one-node GAPBS, one-node ACIC, and eight-node regressions.
It rejects missing/repeated samples, training sources, incomplete matrices,
mixed candidate binaries/flags/layouts, and changed GAPBS selections. Every
high-diameter graph must meet the median <=1 and worst-source <=1.2 GAPBS
limits, and the median eight-node run must beat its own one-node run. The
regression threshold is the largest absolute log deviation of the duplicate
baseline's per-source medians in that allocation, stated explicitly in the
output. Tests cover missing cells, duplicates, invalid timings and warmups.

The default acceptance graph list includes road-usa-w4-z and the five named
regression inputs, not just the three initial D0 graphs. Preparation job
22218657 creates those missing inputs on work storage, with independent
Dijkstra references. Orkut comes from SNAP's public community dataset.
Large graph and reference files are renamed into place only after successful
completion. Subsequent input preparation can resume without accepting partial
outputs. The existing mesh tile sweep is a separate job, 22218452.

## Reproducible continuation

Use `scripts/delta/onenode_matrix.sbatch CAMPAIGN READER GRAPHS` for training
comparisons of shared, shared+tiled, and shared+tiled+slack against the frozen
build and a duplicate frozen control. `CANDIDATE` uses `acic_reader_final`
with `--process-share auto --reader-tile auto --slack-control off`, identically
on sparse and dense graphs. This policy is an experimental candidate, not
an adopted default. Specify the graph list explicitly for the full regression
suite. Each step supports Slurm `-N 1`, `-N 2`, or `-N 8`.

After stronger GAPBS tuning finishes, submit a second GAPBS allocation with
`--export=ALL,GAP_CONFIRM_JOB=FIRST_JOB` to freeze its training selection and
rerun its held-out sources. `GAP_SELECTION_JOB` is different: it only reruns
bucket diagnostics from an existing completed measurement job.

Inspect results with `python3 benchmarks/onenode_assess.py CAMPAIGN`. For the
final test, use `python3 benchmarks/onenode_accept.py CAMPAIGN --allocation
ACIC8_JOB,GAP1_JOB,ACIC1_JOB,REGRESSION8_JOB --allocation
SECOND_ACIC8_JOB,SECOND_GAP1_JOB,SECOND_ACIC1_JOB,SECOND_REGRESSION8_JOB`.
Missing evidence yields `INCOMPLETE` and exit 2, measured failure `NO-GO` and
exit 1, and a complete passing comparison `PASS` and exit 0.

Build labels may no longer overwrite existing binaries or manifests. New
builds retain a pre-build SHA-256 inventory of the complete source snapshot
and private htram copy as well as compiler flags and the output hash.

L4 is deliberately pending the post-lever measurements required by the plan.
The existing `ControlNode` still waits for all local contributions. Reading
cached per-PE reports from one ready PE would not safely implement L4: reports
could mix coarsening generations or falsely report zero outstanding work.
A process snapshot implementation must preserve that ledger and termination
invariant before the all-PE wait can be removed.

## Latest checkpoint

Mesh tile preparation 22218452 completed. The second two-node L1 mesh24 sweep
22218464 also completed: 64 pieces per PE gave a paired ratio of 0.543,
compared with 0.438 in the first allocation. Both are training sweeps.

The first one-node reader pilot 22218625 completed and every digest passed.
For its two training sources, sharing alone took 1.732 / 1.292 s; sharing plus
reader tiles took 1.153 / 0.780 s; adding live slack took 1.176 / 0.762 s.
This pilot has one measured repetition per source and is not acceptance.
The eight-node reader matrix is queued as 22218785. Post-lever diagnostics
are 22218629 (one node), 22218630 (two nodes), and 22218784 (eight nodes).
Scheduler estimates for the eight-node work extend well beyond the current
session; those results remain necessary for an L4 decision and the final pass.

The acceptance check now has seven passing tests, including complete synthetic
two-allocation PASS, missing second allocation, changed build, and worst-source
NO-GO cases. These validate the checker, not the solver's paper performance.
Future joint GAPBS searches record a configurable per-launch wall-time cap
(60 seconds by default, including loading), since extreme deltas can make
an otherwise subsecond solve take minutes. A timeout is invalid and cannot
win selection. Existing external-harness runs retain their previous timeout
unless this configuration field is explicitly set.

## Completed-job review (2026-09-19)

Rechecked the original reference digests and recorded solve times for 392
A/B runs from jobs 22218463, 22218558, 22218625/26/27 and 22218658 (including
their discarded warmups). Every matrix has its full source/repetition grid;
every stored summary agrees with its raw times. All 549 GAPBS runs from
22218622 passed independent reference digests, as did the six post-lever
diagnostic solves in 22218629/30, with zero remaining histogram entries.

New complete L2 comparisons, four held-out sources and three repetitions:

| job | graph | nodes | shared/frozen, paired median | speedup |
|---|---|---:|---:|---:|
| 22218463 | mesh26-z | 1 | 0.155 | 6.5x |
| 22218463 | road-usa-z | 1 | 0.201 | 5.0x |
| 22218558 | mesh24-z | 2 | 0.294 | 3.4x |
| 22218558 | mesh26-z | 2 | 0.183 | 5.5x |
| 22218558 | road-usa-z | 2 | 0.255 | 3.9x |

Slurm records 22218463 as TIMEOUT at 30 minutes, but both graph matrices and
all their raw solves completed and validated. Retain those complete cells;
do not describe the allocation itself as a clean completion. The road-only
L3 retry 22218658 completed cleanly. Shared/frozen was 0.239 and slack/frozen
0.232 by the paired-ratio statistic, while the medians of absolute source
times were 2.215 and 2.245 seconds. These noisy, small differences still do
not establish a benefit from live slack.

The two-node reader pilot 22218626 completed: sharing alone took 1.216 / 1.094
seconds on its two training sources, and sharing plus tiles took 0.810 / 0.621
seconds, a further 33–43% reduction. The one-node pilot showed 33–40%.
These remain one-repetition training pilots, not held-out confirmation.

The regression pilot 22218627 is a concern: on rmat24, the auto candidate's
median paired ratio is 1.078 and worst source is 1.161, versus 1.037 and
1.051 for the duplicate frozen control. Using ratios of per-source medians,
the worst candidate is 1.146 against an observed allocation floor of 1.051.
Thus this allocation does not meet the regression criterion. It is only one
allocation and one dense graph; it cannot establish the cause. All three
auto features select their off paths on this graph's roughly 31 arcs/vertex.
An isolated current-build off/auto comparison and a second allocation are
needed before attributing the regression or adopting the candidate.

Joint GAPBS search 22218622 completed all 462 joint samples, 36 finalist
confirmation samples, 36 held-out samples and 15 warmups, without failures.
Selections and medians over four held-out source medians are:

| graph | threads | delta | one-node GAPBS seconds |
|---|---:|---:|---:|
| mesh24-z | 120 | 4096 | 0.097570 |
| mesh26-z | 128 | 4096 | 0.415810 |
| road-usa-z | 64 | 32768 | 0.174827 |

No selected delta touches the search boundary. Mesh26 uses all 128 physical
cores. These replace the weaker initial coordinate-search baselines; a second
allocation with the same selections is still required. GAPBS bucket counts
at these settings are 148–197 for mesh24, 344–469 for mesh26, and 1103–1625
for road across the four held-out sources.

Post-lever D0 (sharing + reader tiles + slack, diagnostic builds, first tuning
source) now gives:

| graph | nodes | inactive round participation | changes/V | work share | rounds |
|---|---:|---:|---:|---:|---:|
| mesh24-z | 1 | 1.6% | 14.28 | 88.5% | 163 |
| mesh26-z | 1 | 1.4% | 12.96 | 91.6% | 259 |
| road-usa-z | 1 | 4.5% | 10.12 | 85.3% | 368 |
| mesh24-z | 2 | 3.6% | 15.98 | 84.2% | 145 |
| mesh26-z | 2 | 2.9% | 16.24 | 88.9% | 225 |
| road-usa-z | 2 | 7.0% | 15.47 | 80.7% | 355 |

Work timers include queue/locking costs, and round participation is not a CPU
idle measurement. Nevertheless, the original inactivity has largely gone,
and changes/V remains well above the plan's 2–3 target. These one/two-node
signals favor investigating remaining work/rework and shared-queue cost;
they do not establish that cheaper rounds are the next lever. Eight-node
post-lever diagnosis is still needed for the conditional L4 decision.

Paper input job 22218657 completed road-usa-w4 (including Morton order), orkut,
rmat25, uniform25, rmat26 and rmat27. Each has two training and four held-out
Dijkstra references; file sizes match their graph headers. All six remaining
jobs are eight-node jobs: 22218064, 22218229, 22218280, 22218344, 22218784,
22218785. None had started at this review. No final paper pass or default
adoption is claimed.
