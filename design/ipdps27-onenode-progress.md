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
