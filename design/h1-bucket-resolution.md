# H1: bucket width has no resolution on RMAT

*Step 6 of `design/sc27-plan.md`. Method and instrument in
`design/scale-free-diagnosis.md`. Measurements: 2^20 vertices, 16 edges per
vertex, 16 PEs on one exclusive Delta CPU node, median of five.*

## Verdict

**Refuted, and the sign is the other way round.** The controller has roughly
twice the dynamic range on RMAT that it has on the uniform graph, and nine times
the range it has on the mesh. The graph class where the percentile cut really
has degenerated to "admit the frontier bucket and nothing else" is the mesh —
which ACIC wins on.

There is a real finding underneath, and it is not about resolution. See
*What replaces it* below.

## The claim

From the plan: *"`bucket(d) = d/log(V)` is fixed at startup and derived from
nothing but `|V|`. RMAT's small diameter collapses the range into a handful of
the 2048 buckets, so percentile thresholds have nothing to cut and the
controller degenerates toward plain distributed control."*

Two testable parts. The range claim is structural and needs no timing. The
degeneracy claim is behavioural, and its sharpest test needs no new code at all:
if the percentile has nothing to choose, **moving the percentile knob should do
nothing**, and those are numbers the 2024 paper already reports as a parameter
study.

## The range does not collapse

Buckets ever receiving an update, out of 2048:

| | updates created | buckets used | range | buckets holding half the updates |
|---|---|---|---|---|
| RMAT | 19,480,265 | **199** | 0–225 | 36 |
| uniform | 16,770,573 | **204** | 1–209 | 36 |
| mesh | 6,097,495 | 478 | 0–477 | 167 |

RMAT and the uniform graph occupy the same number of buckets to within 2.5%,
and concentrate half their updates into the same 36 of them. Whatever
distinguishes the two graph classes, it is not how much of the bucket array they
reach.

**A different structural fact falls out of the same table, and it is worth
keeping.** `histo_reduction_width` is 256, and the occupied range on both
random-graph classes is under 226. The sliding reduction window is therefore
*wider than the entire live distance range* on both, and covers everything from
the first round onwards — `window_first` never exceeds 135 on the uniform graph
and 83 on RMAT. The window mechanism is inert on both, and does real work only
on the mesh, whose range is 478. Ten-elevenths of the 2048-bucket array is never
touched by any of the three.

## The controller is *least* degenerate on RMAT

Per round, `heap_threshold − first_nonzero` is how far above the frontier the
percentile cut lands — the controller's actual choice for that round. Zero means
it had none.

| | mean cut above frontier | rounds where the cut *is* the frontier | rounds |
|---|---|---|---|
| RMAT | **18.5 buckets** | **6%** | 239 |
| uniform | 9.7 | 26% | 357 |
| mesh | **2.2** | **60%** | 17,954 |

The hypothesis predicts this table with RMAT and mesh exchanged.

## Neither knob is mistuned for RMAT

**Heap percentile**, holding everything else fixed. `p_heap` = 0.999 admits
essentially everything, which is the controller switched off:

| p_heap | RMAT s | RMAT rej/\|E\| | uniform s | uniform rej/\|E\| | mesh s | mesh rej/\|E\| |
|---|---|---|---|---|---|---|
| 0.001 | 0.2089 | 1.133 | 0.2833 | 0.935 | 3.030 | 1.133 |
| 0.005 *(default)* | 0.2043 | 1.161 | 0.2482 | 0.935 | 2.655 | 1.087 |
| 0.05 | 0.1936 | 1.127 | 0.1864 | 0.939 | 2.224 | 1.056 |
| 0.5 | 0.1870 | 1.198 | 0.2346 | 0.978 | 1.796 | 1.743 |
| 0.999 | **0.1788** | 1.129 | 0.2274 | 0.972 | 1.005 | **5.944** |

**Bucket width**, as a multiple of what the `log V` / `sqrt V` rule picks. Best
setting against the current rule: RMAT **1.09×**, uniform **1.04×**, mesh
**1.70×** (and the mesh's optimum is at 1/16 of the current width, the far end of
the sweep). The width rule is mistuned — for the mesh.

## What replaces it

Read the `rej/|E|` columns above rather than the seconds. They say what the
work-admission mechanism is *for*: deferring an update until its bucket is
admitted, so that when it is finally relaxed it carries a distance good enough
not to cause a cascade of further relaxations.

Turning the controller off (`p_heap` 0.005 → 0.999) costs:

- **mesh: 5.5× more rejected updates** (1.087 → 5.944). The mechanism is doing
  enormous work.
- **uniform: 4% more** (0.935 → 0.972). Doing a little.
- **RMAT: nothing at all** (1.161 → 1.129 — it went *down*, within run-to-run
  spread). Doing nothing.

And on RMAT the run with the controller switched off is **1.14× faster** than the
default, because the bookkeeping is not free.

So the hypothesis had the right instinct — the controller is not earning its
keep on RMAT — and the wrong mechanism entirely. It is not that the buckets are
too coarse to separate the work. It is that **on a power-law graph the redundant
work is not order-dependent, so no ordering can prevent it.** RMAT's redundancy
is hub contention: many sources relax the same high-in-degree vertex at
almost the same time with almost the same distance. Those updates land in the
same bucket, so a finer bucket does not separate them and a percentile cut does
not defer one behind another. On a mesh the redundancy is genuinely
order-dependent: relaxing a vertex early with a poor distance propagates a wave
of relaxations that arriving in the right order would have avoided, and that is
what the 5.5× measures.

This is a better result than the hypothesis would have been. It says the
scale-free deficit is not a tuning problem in ACIC's central mechanism, and it
says where the redundancy that ordering cannot touch has to be attacked instead:
`design/h2-hub-redundancy.md`.

## Consequences for step 7

- **Do not derive the bucket width from the observed distribution.** The plan
  proposes it as H1's fix. It buys 9% on RMAT and 4% on the uniform graph, and
  it is worth 1.7× on the mesh — so it is worth doing, but it belongs to the
  high-diameter story and must not be presented as the scale-free fix.
- **`HISTO_BUCKET_COUNT` 2048 and `histo_reduction_width` 256 are both
  oversized** relative to what the width rule produces, by roughly 10× and 1.3×.
  The window never slides on a random graph. If the width is ever derived from
  the distribution, these two have to move with it or the window will start
  sliding when it did not before — a behaviour change disguised as a tuning
  change.
- **The `p_heap` curve is not monotone on the uniform graph** (0.186 s at 0.05
  against 0.227 s at 0.999) and is monotone on the mesh. Any adaptive
  percentile controller has a non-convex objective on at least one input class,
  which is worth knowing before one is written.
