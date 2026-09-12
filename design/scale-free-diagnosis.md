# Why scale-free graphs lose: the step 6 diagnosis

*Step 6 of `design/sc27-plan.md`. Four hypotheses, each measured as an A/B with
everything else held fixed. This file is the method and the instrument; the
answers are in `design/h1-bucket-resolution.md`, `design/h2-hub-redundancy.md`,
`design/h3-partitioning.md` and `design/h4-tail-cadence.md`.*

## The gap being explained

The IA³@SC24 paper reports RIKEN's Δ-stepping at **2.8–3.3× faster than ACIC on
RMAT**, while ACIC wins on the graph classes with a diameter worth speaking of.
The plan names four candidate causes. They are not exclusive and they are not
equally cheap to test, so the order here is by cost of measurement, not by
expected size of effect:

| | Hypothesis | What it predicts you would see |
|---|---|---|
| **H1** | The bucket rule gives the controller no resolution on a small-diameter graph | Few of the 2048 buckets occupied; percentile thresholds land on the same bucket every round; performance insensitive to the percentile knobs but sensitive to bucket width |
| **H2** | Redundant updates pile onto hubs | Reject rate rising with destination degree; arrivals concentrated on a small share of vertices; a large batch-local combining ceiling |
| **H3** | A contiguous 1-D partitioning imbalances on a power law | Per-PE edge and work counts skewed, and getting worse as PEs are added |
| **H4** | The tail advances only at the controller's cadence | A long stretch at the end with rounds but almost no settled vertices, and a compute time that grows with an artificially lengthened round period |

## Two builds, on purpose

`sssp_smp` is unchanged on the relaxation path. **Every wall-clock number comes
from this binary.**

`sssp_smp_diag` (`make sssp_smp_diag`, `-DACIC_DIAG -DVCOUNT`) adds counters
that sit directly in `process_update` and `generate_updates`: a cumulative
per-bucket creation profile, a per-vertex arrival count, degree-binned arrival
and reject counts, and a duplicate count over each delivered batch. **Every
structural number comes from this binary**, and none of them is a function of
the clock — an arrival is an arrival whether or not counting it cost 3 ns.

Mixing the two would be the same mistake step 5 found with peak RSS: measuring
something real with an instrument that cannot see it. The gate runs three
configurations through `sssp_smp_diag` for exactly this reason — its counters
are on the hot path, so they can be wrong, and a wrong counter that still
produces the right distances would otherwise go unnoticed.

## What the instrument records

`--diag <prefix>` writes, at the end of a run and from PE 0 only:

| File | Contents | Built in |
|---|---|---|
| `<prefix>.rounds.csv` | one row per controller round: wall time, histogram sum, window position, occupied buckets, span, both thresholds, the update counters, settled vertices | any build |
| `<prefix>.buckets.csv` | updates ever created in each of the 2048 buckets | `ACIC_DIAG` |
| `<prefix>.vcount.csv` | vertices whose tentative distance sits in each bucket, plus the unreached | `VCOUNT` |
| `<prefix>.degree.csv` | vertices, out-edges, arrivals and rejects, binned by destination out-degree | `ACIC_DIAG` |
| `<prefix>.arrivals.csv` | vertices and arrivals binned by how much traffic each vertex received | `ACIC_DIAG` |

and one `DIAG_PE` line per PE into the run log, for the per-PE spread H3 needs.
A spread is not a sum or a maximum, so it is printed rather than reduced.

Two knobs exist in the production build purely so the A/Bs can be run without
rebuilding, and both are inert at their defaults:

- `--bucket-width <w>` replaces the width the code derives from `|V|`.
- `--round-delay <ms>` inserts a delay into the controller's cycle, which is
  otherwise closed — each chare calls `contribute_histogram()` at the end of
  `current_thresholds()`, so nothing sets a period at all today.

## Choosing a source vertex is part of the experiment

A third of an RMAT graph's vertices have no out-edges. A source with none
produces no updates, so the convergence test — which wants
`updates_created > 1000` before it will believe a run finished — never fires and
the run sits until `--timeout`. Picking a source by hand per configuration is
how a measurement stops being reproducible, so the harness asks
`tools/graph_convert source` for one, by a stated rule: **the lowest-numbered
vertex whose out-degree is at least the graph's mean.** Deterministic, the same
at every PE count, and deliberately not "the largest hub", which would be a
different experiment.

Reachability differs sharply between the classes at equal `|V|` and this must be
carried alongside every number: an RMAT run is solving a smaller problem than a
uniform run on the same vertex count, and a speed comparison that ignores that
is measuring the generator.

## Reproducing

```
scripts/diagnose.sh <h1|h2|h3|h4|all> [outdir]   # runs the matrix
scripts/diag_report.py <outdir>                  # prints the tables
sbatch scripts/diagnose_delta.sbatch all 20      # the reported runs
```

A login node is fine for shaking the harness out and useless for anything
timed: H1 and H4 compare runs against each other, and on a shared node the
difference between two configurations is smaller than the difference between two
neighbours. The reported runs are on an exclusive Delta CPU node with
`+setcpuaffinity`, median of five repetitions.
