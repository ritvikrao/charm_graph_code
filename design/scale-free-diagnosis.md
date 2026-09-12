# Why scale-free graphs lose: the step 6 diagnosis

*Step 6 of `design/sc27-plan.md`. Four hypotheses, each measured as an A/B with
everything else held fixed. This file is the method and the instrument; the
answers are in `design/h1-bucket-resolution.md`, `design/h2-hub-redundancy.md`,
`design/h3-partitioning.md` and `design/h4-tail-cadence.md`.*

## Result

**Three of the four hypotheses are refuted for RMAT and confirmed for the mesh.**
That is not four separate answers; it is one answer, and it is the most useful
thing step 6 produced.

| | verdict on RMAT | verdict on the mesh |
|---|---|---|
| **H1** bucket resolution | refuted. The controller has 18.5 buckets of range above the frontier per round, against the uniform graph's 9.7; the percentile knob moves runtime by 1.17× and redundant work not at all | confirmed. 2.2 buckets of range, 60% of rounds with none at all, and switching the controller off costs **5.5× more rejected updates** |
| **H2** hub redundancy | **confirmed.** Reject rate climbs monotonically with destination degree, 19.6% → 100%; 2.07% of vertices take 65.4% of arrivals and 70.0% of rejects; the uniform control is flat at 84.9% in every degree class | not applicable — no hubs, though the mesh absorbs more per batch than RMAT does, for an unrelated reason |
| **H3** partition imbalance | refuted. 1.19× at 32 PEs, in a region the calibration shows costs nothing; PEs idle 9% of rounds | confirmed, but not spatially. Edge balance is 1.00×, work imbalance 1.94×, and PEs sit out **85% of rounds — including on a single PE**, where there is no partition |
| **H4** cadence-bound progress | refuted. Flushing every round is 4% *slower*; buffers fill on their own | confirmed overwhelmingly. Flushing every round instead of one in five is **3.2× faster**; adding 4 ms per round multiplies runtime 27.7× with the round count unchanged |

Read down the right-hand column: H1, H3 and H4 are all real, large, fixable
problems **of the graph classes ACIC already wins on.** Fixing them widens an
existing lead. None of them closes the 2.8–3.3× deficit on RMAT, and a paper
that presented them as the scale-free fix would be making a claim these tables
refute.

What is left for RMAT is H2, and one finding that no hypothesis anticipated:
**on a power-law graph the redundant work is not order-dependent, so no ordering
can prevent it.** Switching ACIC's work-admission off entirely changes RMAT's
redundant work by nothing measurable (1.161 → 1.129 rejects per edge) and makes
the run 1.14× faster, because the bookkeeping is not free. The same switch costs
the mesh 5.5× its redundant work. ACIC's central mechanism is not mistuned on
scale-free graphs; it has nothing to bite on, because hub contention puts the
competing updates in the same bucket at the same time. That is a combining
problem, which is H2, and it is the only place left to attack.

The one caution against over-reading all of this: the high-diameter class here
is a 2-D mesh, which is a stand-in for a road network and not a substitute for
one. A real road network is the confirmation these conclusions need, and it is
the obvious next input for `graphlib` now that the reader exists.

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

## What step 7 should take from this

In priority order, which is not the plan's order:

1. **Adaptive flush cadence.** 3.2× on one node and 3.4× on two, on the mesh,
   from one existing constant. The policy is legible from the data — flush
   aggressively when buffers are not filling, lazily when they are — and the
   decision variable is buffer occupancy, which htram already knows. See
   `design/h4-tail-cadence.md`.
2. **`CombiningHold` before the batch-local fold.** The plan has these the other
   way round. The hold's reach does not shrink with problem size; the
   batch-local fold's does, from 41.6% to 9.9% over the sizes measured. See
   `design/h2-hub-redundancy.md`.
3. **Bucket width from the observed distribution** — worth 1.7× on the mesh, 9%
   on RMAT. Do it, and do not sell it as the scale-free fix. It also requires
   moving `HISTO_BUCKET_COUNT` and `histo_reduction_width`, which are currently
   10× and 1.3× oversized relative to what the width rule produces.
4. **Not measurement-based load balancing.** There is no spatial imbalance to
   correct, and the class that starves its PEs does so on a single PE.

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
