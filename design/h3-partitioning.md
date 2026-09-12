# H3: 1-D partitioning imbalances on a power law

*Step 6 of `design/sc27-plan.md`. Method and instrument in
`design/scale-free-diagnosis.md`. Measurements: 2^20 vertices, 16 edges per
vertex, one exclusive Delta CPU node, `--partition-jitter 0` throughout.*

## Verdict

**Refuted as a spatial problem, confirmed as a temporal one, and the temporal
one belongs to the wrong graph class.** A contiguous 1-D partitioning of a
power-law graph at 32 PEs is imbalanced by 1.19× in edges, and the calibration
below says an imbalance that size costs nothing measurable. The graph whose PEs
are actually starved is the mesh — which is perfectly balanced in edges
(1.00×) and whose PEs sit out **85% of all controller rounds**.

The plan's own expectation was that measurement-based migration would be a poor
fit because SSSP's imbalance is temporal. That is right, and it is now measured
rather than argued — but it is a statement about high-diameter graphs, not about
scale-free ones.

## A confound that had to be removed first

Mode 1 — the uniform graph — draws each PE's share of the vertices at random
within ±20% of `V/N`. Modes 2 and 3 divide evenly. So the obvious experiment,
comparing per-PE load between the uniform graph and RMAT, was comparing a
deliberately skewed partition against an even one, and the injected skew is the
same order as anything a power law produces. Everything below runs at
`--partition-jitter 0`; the jitter is reused as a calibration instead.

## Static balance

Maximum over PEs divided by the mean, at `--partition-jitter 0`:

| PEs | RMAT edges | uniform edges | mesh edges | RMAT updates processed | mesh updates processed |
|---|---|---|---|---|---|
| 4 | 1.01 | 1.00 | 1.00 | 1.01 | 1.02 |
| 8 | 1.04 | 1.00 | 1.00 | 1.04 | 1.20 |
| 16 | 1.08 | 1.01 | 1.00 | 1.08 | 1.40 |
| 32 | **1.19** | 1.01 | 1.00 | **1.19** | **1.94** |

The power law does produce imbalance and it does grow with PE count — 1.19 at 32
PEs, and it will keep growing, since the vertices per PE keep falling while the
degree distribution does not. But look at the last column: **the mesh's work
imbalance at 32 PEs is 1.94, worse than RMAT's, on a partition that is balanced
in edges to three decimal places.** Static edge balance and work balance are not
the same quantity, and on a frontier algorithm the first does not predict the
second.

## Calibration: what an imbalance of that size costs

An imbalance measurement that reads ~1.0 everywhere is indistinguishable from a
broken one, so the uniform mode's jitter is turned up deliberately to give the
instrument something to find:

| jitter | edges max/mean | work max/mean | compute s | rej/\|E\| | vs even |
|---|---|---|---|---|---|
| 0% | 1.01 | 1.00 | 0.2290 | 0.9350 | 1.00× |
| 10% | 1.16 | 1.16 | 0.2119 | 0.9350 | 0.93× |
| 20% | 1.32 | 1.32 | 0.2203 | 0.9353 | 0.96× |
| 40% | 1.64 | 1.63 | 0.3036 | 0.9355 | **1.33×** |

The instrument works: injected skew shows up in the reading, proportionally.
And the answer to "what does it cost" is **nothing up to about 1.3×, and 33% at
1.64×**. RMAT's 1.19 at 32 PEs sits in the region where the effect is below the
run-to-run spread. It is not a 2.8–3.3× gap and it is not on the path to one.

(The 10% and 20% rows coming in slightly *under* the even partition is
measurement spread, not a finding. It is quoted rather than smoothed because it
is the honest width of the noise band, and it is what tells you the 1.19 result
is a null.)

## Temporal starvation, which is the real effect

`idle_rounds` counts controller rounds in which a PE processed no updates at
all. Mean across PEs:

| PEs | RMAT | uniform | mesh |
|---|---|---|---|
| 1 | 9% | 6% | 56% |
| 8 | 11% | 15% | 83% |
| 16 | 6% | 12% | **85%** |
| 32 | 9% | 19% | 83% |

RMAT's PEs are busy. The mesh's are idle five rounds in six, at every PE count
including **one** — 56% of rounds idle on a single PE, where there is no
partitioning at all and nothing to balance. That rules out partitioning as the
cause by construction: it is a property of how little work a mesh frontier
generates between controller rounds, and it is the subject of
`design/h4-tail-cadence.md`.

## Consequences for step 7

- **Do not build measurement-based load balancing for this.** There is no
  spatial imbalance to correct on RMAT at any PE count measured, and the graph
  that starves its PEs starves them on one PE. Migration cannot move work that
  does not exist yet.
- **The overdecomposition half of the plan's H3 is untestable today and should
  be marked so.** It needs more chares than PEs, and `thisIndex` and `CkMyPe()`
  are used interchangeably throughout `sssp_smp.cpp` — correct only under
  one-chare-per-PE round-robin placement. That is step 9's `Locator` work. What
  can be said now is that the *motivation* offered for it — scattering hubs to
  fix imbalance — is not supported, so if it pays it will be for the other
  reason the plan gives: more schedulable work overlapping the `[whenidle]`
  drain. That is an H4 argument.
- **Report the 1.19 anyway.** It grows with PE count, and the target scale is
  larger than 32 PEs. It is not the explanation for the scale-free deficit, but
  a paper that claims 512-node scaling should say where a 1-D partition's
  balance is heading.
