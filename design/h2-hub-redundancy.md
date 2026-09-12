# H2: redundant updates pile onto hubs

*Step 6 of `design/sc27-plan.md`. Method and instrument in
`design/scale-free-diagnosis.md`. Status: **structural evidence in, at 2^14 and
2^18; awaiting the 2^20 confirmation run.***

## The claim, stated so it can fail

The plan's H2 is one line — "redundant updates to hubs. Addressed by combining."
That is two claims, and only the second is about combining:

1. **A scale-free graph produces more redundant work than a uniform one.**
2. **That redundancy is concentrated**, on a small number of destinations and
   within a short enough window, that a combining table can absorb it.

The first is nearly a tautology and is not what decides whether to build
`CombiningHold`. The second is, and it is the one that can come out false: a
graph can generate enormous redundancy that is spread so thinly across
destinations and time that no table of any affordable size catches any of it.

Three numbers, then, not one:

| Number | Where from | What it bounds |
|---|---|---|
| `rejected / |E|` | already printed by every run | **every** combining scheme. An update rejected on arrival is one a perfect oracle could have suppressed; one that is accepted could not have been |
| batch-local absorb rate | `ACIC_DIAG`, over each delivered batch | what a fold inside the `deliver` callback would catch — the ~30-line version, no hold, no policy |
| reject rate by destination degree | `<prefix>.degree.csv` | whether the redundancy is *at hubs*, which is the actual hypothesis |

The third is the one that distinguishes H2 from "SSSP does redundant work",
which has been known since Δ-stepping.

## What the measurement says

At 2^14 vertices, 16 edges per vertex, 4 PEs. The 2^20 numbers replace these
when the compute-node run lands; the shape is what matters here.

**Redundancy is at hubs on RMAT, and is degree-blind on the uniform graph.**
Reject rate against destination out-degree:

| out-degree ≥ | RMAT vertices | RMAT reject rate | uniform vertices | uniform reject rate |
|---|---|---|---|---|
| 0 | 5489 | 27.5% | 500 | 83.9% |
| 4 | 2212 | 64.5% | 1957 | 83.9% |
| 32 | 827 | 94.0% | 494 | 83.9% |
| 128 | 364 | 97.7% | — | — |
| 512 | 90 | 99.3% | — | — |
| 4096 | 1 | 99.9% | — | — |

The uniform column is the control and it is doing its job: **flat to a tenth of
a percent across every degree class it has.** Whatever produces the uniform
graph's redundancy, it is not degree. RMAT's climbs monotonically from 27% to
99.9%, and 59% of all its rejected updates land on the 2.9% of vertices with
out-degree 128 or more.

**The traffic is concentrated, which is what a table needs.** Arrivals per
vertex, busiest first, on RMAT: the top 0.65% of vertices take 30% of all
arrivals; the top 2.9% take 55%. On the uniform graph the same curve is flat —
55% of the vertices take 65% of the arrivals, which is what "no concentration"
looks like.

**Batch-local combining alone clears the break-even by a wide margin.**
Share of delivered items repeating a destination already in their own batch:

| buffer size | uniform | mesh | RMAT |
|---|---|---|---|
| 128 | 0.3% | 31.6% | 19.4% |
| 512 | 1.5% | 46.6% | 26.9% |
| 2048 | 5.2% | 46.1% | 41.6% |

Against a stated break-even of 8–12%, RMAT clears it at every buffer size from
512 up and the uniform graph never does. That is the adaptive-policy figure the
plan wants, and it falls out of the buffer size alone.

## The finding that contradicts the plan's expected narrative

The plan proposes to present the on/off policy as *"the system turns combining
off on road networks and on for RMAT, automatically."* **The mesh absorbs 46% —
more than RMAT does.** On the strength of the absorb rate alone, combining would
switch *on* for the high-diameter graph too, and the sentence as drafted would
be describing something the system does not do.

The two rates have different causes, and the instrument separates them. RMAT's
concentration is **structural**: a few vertices have enormous in-degree, so a
batch drawn from anywhere in the graph repeats them. The mesh's is **temporal**:
degree 4 everywhere and no vertex is special, but the frontier is a slowly
advancing wave over a small set of vertices, so a batch drawn from a narrow
frontier repeats its members. The arrival-concentration table distinguishes them
directly — the mesh's busiest 0.9% of vertices take 3.3% of arrivals, which is
almost exactly their proportional share.

This matters for step 7 beyond the wording. A policy keyed on the *absorb rate*
will turn combining on for both, and that is probably right — a 46% fold is
worth having wherever it comes from. But the claim the paper can defend is
narrower than "combining is the scale-free fix": combining pays wherever traffic
concentrates, and traffic concentrates for two unrelated reasons. A road-network
input is needed to say which of the two a real high-diameter graph resembles,
and the mesh is not a substitute for that.

## What this does not measure, and why

**The source-side ceiling.** The plan notes that on power-law graphs the
dominant redundancy is *across* source PEs, and that source- and receiver-side
combining must be reported separately. Only the receiver side is measured here,
because the source side needs a hold that outlives a single buffer — which is
`CombiningHold`, which is step 7. What can be said now is that the two are
bracketed: batch-local absorb is a lower bound on what combining catches, and
`rejected / |E|` an upper bound on what any scheme could. On RMAT at 2^14 those
are 41.6% and 1.71 rejects per edge, so the gap left for a longer-lived hold is
large.

**Whether absorbing costs anything.** `on_absorb` is a correctness requirement,
not a metric: the application owns `histogram[]` and `updates_created`, and
silently destroying an item makes the termination predicate unreachable. Nothing
here measures the bookkeeping that has to accompany a fold.

## Verdict

**Supported, with a correction to the framing.** The redundancy on a scale-free
graph is concentrated by degree in a way the uniform graph's is not, and a fold
no more complicated than a hash table over one delivered batch would absorb
40%+ of it. The correction is that a high absorb rate is not by itself evidence
of a hub problem, and the mesh proves it.
