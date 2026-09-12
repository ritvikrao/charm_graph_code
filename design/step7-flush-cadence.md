# Step 7.1 — adaptive flush cadence

**Result: `--flush-policy adaptive`, now the default, is 3.6× faster on the mesh
on one node and 3.8× on two, and no slower on RMAT or the uniform graph on
either.** It reaches step 6's flush-every-round bound on the mesh without paying
that bound's 3–16% cost on the random graphs. The first policy tried did pay it,
on two nodes, and is kept as `--flush-policy stale` so the A/B that rejected it
can be rerun.

As `design/h4-tail-cadence.md` said it must be reported: this widens ACIC's lead
on the class it already wins on. It does nothing for the scale-free deficit.

## Mechanism

A partly filled aggregation buffer leaves htram in exactly two ways: it fills to
`bufSize`, or a flush reaches it. Before this change the only flush was a
per-chare draw, one round in five on average. On the mesh the buffers never fill,
so the whole run advanced one draw at a time — step 6 measured the round count
as inversely proportional to the draw rate over a factor of twenty.

The adaptive policy adds flushes; it never removes the draw. In a round where it
applies, each chare flushes **only the destinations whose buffer did not fill on
its own since the previous round** (`HTram::flushStale`, one counter per
destination incremented at every full-buffer send). A destination that is
shipping full buffers is left alone.

It applies only in rounds the controller marks **starved**: fewer items in the
reduced histogram window than one `bufSize` per (sender PE, destination node)
stream, `histogram_sum < N · nodes · bufSize`. Main computes this in
`reduce_histogram` and broadcasts it with the thresholds; `--diag` records it per
round.

`HTram::flushDest` drains one destination the way `tflush()` does, with the
`ADD_FILLERS` padding the library is built with, so the two flushes differ only
in which destinations they reach. It drains the held items before shipping the
partial buffer, where `tflush()` ships first, which saves a message.

## Why the first policy was wrong

`stale` is the per-destination rule applied every round, with no gate. On one
node it looked fine: 15 repetitions put it at 0.97× on RMAT and 1.03× on the
uniform graph. On two nodes it is not:

| 2 nodes, 32 PEs | mesh | RMAT | uniform |
|---|---|---|---|
| `stale` | 0.26× | **1.07×** (1.12× in a second job) | **1.12×** |
| `stale` flushes / streams × rounds | 43,643 / 191,616 | 6,695 / 13,440 | 5,778 / 21,376 |
| `stale` messages vs fixed | −4% | **+25%** | **+26%** |

A two-node round is a fraction of a millisecond, which is shorter than an RMAT
stream takes to fill a 2048-item buffer mid-run. "Did not fill since the last
round" is therefore true of about half the streams in any round, and the policy
turns into flush-every-round for them — which step 6 had already measured as a
loss on RMAT.

The gate comes from step 6's round series at 2^20 on one node. The in-flight
population separates the two regimes cleanly:

| | median `histogram_sum` | max | rounds below 16 × 2048 |
|---|---|---|---|
| mesh | 2,666 | 8,426 | **100%** |
| RMAT | 261,654 | 1,413,966 | 28% |
| uniform | 407,333 | 1,756,756 | 22% |

The mesh never has a buffer's worth of work per stream in flight, so it is
starved every round and flushes like `stale`. The random graphs are starved only
at the start and in the tail, which is where a flush is cheap and useful, and
behave like `fixed` for the bulk of the run. On two nodes RMAT's stale flushes
fall from 6,695 to 1,701 and the uniform graph's from 5,778 to 176.

## Measurements

2^20 vertices, 16 edges per vertex for RMAT and uniform, a 1024 × 1024 mesh,
16 PEs per node, exclusive Delta CPU nodes, `+setcpuaffinity`, median of 5
repetitions with the variants interleaved inside each repetition. Ratios are
against `fixed` in the same job; absolute times differ by up to 45% between
nodes (RMAT's baseline was 0.225 s on cn116 and 0.154 s on cn047), so no number
here is compared across jobs.

One node (`design/step7-data/7.1/1node-gated.out`, cn022):

| variant | mesh | rounds | RMAT | rounds | uniform | rounds |
|---|---|---|---|---|---|---|
| `fixed` | 2.333 s | 17,960 | 0.238 s | 277 | 0.229 s | 372 |
| flush every round | 0.27× | 3,792 | 1.05× | 303 | 1.03× | 379 |
| `stale` | 0.28× | 3,730 | 0.94× | 254 | 1.06× | 379 |
| **`adaptive`** | **0.28×** | 3,734 | **0.98×** | 296 | **1.06×** | 398 |

Two nodes (`design/step7-data/7.1/2node-gated.out`, cn[022,053]):

| variant | mesh | rounds | RMAT | rounds | uniform | rounds |
|---|---|---|---|---|---|---|
| `fixed` | 1.680 s | 15,879 | 0.129 s | 227 | 0.122 s | 327 |
| flush every round | 0.28× | 3,111 | 1.16× | 209 | 1.11× | 324 |
| `stale` | 0.26× | 2,994 | 1.07× | 210 | 1.12× | 334 |
| **`adaptive`** | **0.27×** | 2,994 | **0.95×** | 202 | **0.99×** | 338 |

The one-node uniform 1.06× is noise, and the logs say so directly: `adaptive`
flushed 36 destinations in 398 rounds on that graph, which cannot move a
0.23 s run by 6%. The spreads overlap (0.216–0.246 against 0.223–0.261).

Rejected updates per edge do not move on the random graphs. On the mesh they
fall, 1.754 → 1.368 on two nodes, because work that leaves promptly is relaxed
with fresher distances.

## Correctness

`scripts/verify.sh` passes all 18 configurations plus the 3 diagnosis-build
checks under each of `--flush-policy fixed`, `stale` and `adaptive`, and under
the new default. The gate's new `SSSP_EXTRA_ARGS` hook runs it with any solver
flag set. The digests do not change: they are the answer, not a property of the
schedule.

## What is left

- **The 1/5 draw is still there**, as a floor. On the random graphs in the middle
  of the run it is the only flush, and nothing here says one in five is right —
  only that the starved rounds do not need it.
- **The gate counts the window, not the buffers.** `histogram_sum` includes
  work sitting in heaps and holds, not just in aggregation buffers, so it
  over-estimates what could fill a buffer. That errs towards *not* flushing —
  towards the step 6 behaviour, rather than towards the regression `stale`
  showed. Buffer occupancy is known inside
  htram, but it is per PE, and the whole point of the gate is a global view.
- **Idle flush (step 7.4) is a different trigger for the same problem.** This
  policy acts at round boundaries; an idle-triggered flush acts when a PE runs
  out of work. It should be measured on top of this one, not instead of it.

## Reproducing

```
scripts/stage_scratch.sh /scratch/.../s71-1n
cd /scratch/.../s71-1n/charm_graph_code
sbatch      scripts/ab_delta.sbatch scripts/ab/7.1-flush-policy.txt 20
sbatch -N 2 scripts/ab_delta.sbatch scripts/ab/7.1-flush-policy.txt 20   # from a second copy
```
