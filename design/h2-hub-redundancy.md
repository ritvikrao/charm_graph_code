# H2: redundant updates pile onto hubs

*Step 6 of `design/sc27-plan.md`. Method and instrument in
`design/scale-free-diagnosis.md`. Measurements: 2^14, 2^18 and 2^20 vertices,
16 edges per vertex, 16 PEs on one exclusive Delta CPU node.*

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

At 2^20 vertices, 16 edges per vertex, 16 PEs.

**Redundancy is at hubs on RMAT, and is degree-blind on the uniform graph.**
Reject rate against destination out-degree:

| out-degree ≥ | RMAT vertices | RMAT reject rate | uniform vertices | uniform reject rate |
|---|---|---|---|---|
| 0 | 501,926 | 19.6% | 31,538 | 84.9% |
| 4 | 96,617 | 60.6% | 127,028 | 84.9% |
| 16 | 69,309 | 83.6% | 507,970 | 84.9% |
| 32 | 12,041 | 92.9% | 31,684 | 84.9% |
| 128 | 15,406 | 97.5% | — | — |
| 512 | 4,845 | 99.1% | — | — |
| 2048 | 1,139 | 99.7% | — | — |
| 16384 | 20 | 100.0% | — | — |
| 65536 | 1 | 100.0% | — | — |

The uniform column is the control and it is doing its job: **84.9% in every
single degree class it has, to one decimal place.** Whatever produces the
uniform graph's redundancy, it is not degree, and the instrument is not
manufacturing a gradient where none exists. RMAT's climbs monotonically from
19.6% to 100.0% across three orders of magnitude of degree.

The 21,700 vertices of out-degree ≥128 are **2.07% of the graph**. They receive
**65.4% of all arrivals** and account for **70.0% of all rejected updates**.

**The traffic is concentrated, which is what a table needs.** Arrivals per
vertex on RMAT, busiest first: the top **0.59%** of vertices take **45.3%** of
all arrivals, and the top 2.07% take 65.4%. On the uniform graph the same curve
is flat — 53% of the vertices take 63% of the arrivals, which is what "no
concentration" looks like.

**The batch-local absorb rate is real, and it is shrinking with graph size.**
Share of delivered items repeating a destination already in their own batch:

| buffer size | uniform 2^14 / 2^18 / 2^20 | mesh | RMAT |
|---|---|---|---|
| 128 | 0.3% / 0.02% / 0.0% | 31.6% / 14.3% / 14.1% | 19.4% / 8.7% / **6.7%** |
| 512 | 1.5% / 0.10% / 0.0% | 46.6% / 29.6% / 31.1% | 26.9% / 10.9% / **6.6%** |
| 2048 | 5.2% / 0.39% / 0.1% | 46.1% / 32.0% / 33.7% | 41.6% / 17.5% / **9.9%** |

RMAT's batch-local absorb rate fell from 41.6% to 9.9% over a 64× increase in
graph size, and it is still falling. This is the single most consequential
number in the note and it points the opposite way to the headline: **at 2^20 a
fold confined to one delivered batch is already at the bottom edge of the stated
8–12% break-even, and the target scale is larger still.** The mesh's rate, by
contrast, has stabilised around 32% — its frontier width is set by the side
length, not the vertex count.

The reason is mechanical. A batch is at most `bufSize` items drawn from
whatever the frontier is emitting at that moment. As the graph grows the
frontier grows with it, so the chance that two items in the same 2048 repeat a
destination falls — regardless of how concentrated the traffic is *overall*.
The concentration measured above does not go away with scale; the window in
which a batch-local table can see it does.

Against the stated 8–12% break-even, RMAT at 2^20 is at 9.9% with a 2048-item
buffer and 6.7% with a 128-item one. A mechanism that has to be switched off
below 8% would be switching itself off on the very input it was built for,
somewhere between here and the scale the paper reports.

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
`rejected / |E|` an upper bound on what any scheme could. On RMAT at 2^20 those
are 9.9% and 1.14 rejects per edge against an overall arrival reject rate of
**92.3%** — so the gap left for a longer-lived hold is enormous, and the scale
trend above says it is *widening*, because the upper bound is a property of the
graph while the lower bound is a property of the buffer.

That reframes what step 7 should build first. The plan's ordering treats the
batch-local fold in `deliver` as the cheap addendum to `CombiningHold` — "~30
lines, fits in L2". On this evidence the priority is the other way round: the
hold is the part whose reach does not shrink as the problem grows, and the
batch-local fold is the part that needs its own scaling argument before it goes
in a paper.

**Whether absorbing costs anything.** `on_absorb` is a correctness requirement,
not a metric: the application owns `histogram[]` and `updates_created`, and
silently destroying an item makes the termination predicate unreachable. Nothing
here measures the bookkeeping that has to accompany a fold.

## Verdict

**Supported, with two corrections to the framing.**

The hypothesis itself holds and holds cleanly: redundancy on a scale-free graph
is concentrated by destination degree in a way the uniform graph's is not, and
the uniform control is flat to a tenth of a percent, which is as good as a
control gets.

The first correction is that a high absorb rate is not by itself evidence of a
hub problem — the mesh has one and has no hubs. The second is that RMAT's
batch-local absorb rate fell from 41.6% to 9.9% between 2^14 and 2^20 and should
be expected to keep falling, so the figure that makes combining look
overwhelming at toy scale is the figure least likely to survive to the scale the
paper reports. Neither undermines building `CombiningHold`; both change what may
be claimed for it and in what order the pieces are worth building.

**H2 is also the only one of the four hypotheses that survived.** H1, H3 and H4
were each refuted for RMAT and confirmed for the mesh — see
`design/scale-free-diagnosis.md` for what that pattern means for the paper.
Combining is not one candidate fix among four; on this evidence it is the only
one that addresses the graph class the paper needs to explain.
