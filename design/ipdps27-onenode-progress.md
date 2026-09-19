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

No optimization has been adopted yet. L1 reader placement and L4 remain
conditional on the measurements required by the original plan.

## L1 relabeling experiment

`reorder_graph.py --tile T --pes P` deals compact tiles round-robin,
concatenates each owner's tiles, remaps physical sources, and checks independent
Dijkstra distance aggregates. `ordered` accepts an already Morton-ordered input.
Cut reporting now reproduces the existing reader's equal-edge partition loop;
assuming equal vertex ranges was inaccurate. Those edge-balanced boundaries may
split the intended tile-owner groups, so the report explicitly records this.
The solver's reader is unchanged pending evidence that tiling pays.

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
