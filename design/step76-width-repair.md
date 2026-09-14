# Step 7.6d — the width repair

Item 2 of the 7.5 next work ended with a diagnosis and a menu, not a change:
`design/step76-controller.md` names three candidate repairs, rules one of them
out, and says the choice belongs to a later step. This is that step. Two of the
three are built here, together, because neither is sufficient alone.

## What shipped

**`--bucket-width-rule logv|weight`, default `weight`.** The width the file and
random-graph modes derive is no longer `log(V)`, the natural log of the vertex
count. It is the heaviest edge in the graph, so the histogram's 2048 buckets
span 2048 maximum-weight edges rather than 2048 units of `log |V|`.

**`--clamp-freeze off|on`, default `on`.** `coarsen_buckets` no longer raises
`bucket_limit` alongside `bucket_scale`, and no longer merges bucket 2047. The
clamp bucket becomes an overflow slot at a fixed distance instead of an index,
increment and decrement both land on it for the life of the run, and the
`--coarsen-clamped` guard has nothing left to protect.

Both old behaviours are still reachable by flag, so one binary runs every arm
and no comparison straddles a build.

## The width rule, and why it is a rule and not a bound

The controller is blind above `2048 * width`: everything past it is charged to
bucket 2047, which means "at or beyond the top of the range" and carries no
ordering. A graph is therefore representable only while
`width > max_distance / 2048`, and `log(V)` -- a function of the vertex count,
which has nothing to do with the distances being bucketed -- clears that bar by
accident or not at all.

What the repair needs is an estimate of the distance range before the solve
starts. **There is no cheap sound bound available.** The only global the solver
already computed was `max_sum`, the sum over every vertex of its heaviest
out-edge. It does bound the cost of any simple path, but on mesh20 it is order
8e8, so `max_sum / 2048` would be a width near 400,000 against a range of
246,154: the entire graph in bucket 0, which is the opposite failure and a
worse one. Nothing tighter is available without a traversal, and a traversal is
a second solve.

So the rule is a rule. `width = max_edge_weight` spans the histogram over 2048
maximum-weight edges, and it is sufficient exactly when the deepest branch of
the shortest-path tree averages no more than `max_weight * 2048 / depth` per
hop. That is a statable condition, it is false in general -- a path of more
than 2048 maximum-weight edges breaks it -- and it holds on every graph the
campaign uses:

| graph | V | `max_weight` | max distance | needs width > | `log(V)` short by | new width | span / range |
|---|---:|---:|---:|---:|---:|---:|---:|
| road-ny | 264,346 | 36,946 | 1,290,145 | 630.0 | **50.5x** | 36,946 | 58.6x |
| mesh22 | 4,194,304 | 1,000 | 506,463 | 247.3 | 16.2x | 1,000 | **4.0x** |
| mesh20 | 1,048,576 | 999 | 246,154 | 120.2 | 8.7x | 999 | 8.3x |
| youtube | 1,134,890 | 1,000 | 7,285 | 3.6 | safe | 1,000 | 281x |
| rmat22 | 4,194,304 | 1,000 | 3,161 | 1.5 | safe | 1,000 | 648x |
| rmat20 | 1,048,576 | 1,000 | 3,446 | 1.7 | safe | 1,000 | 594x |
| rmat20-s2 | 1,048,576 | 1,000 | 2,293 | 1.1 | safe | 1,000 | 893x |
| uniform20 | 1,048,576 | 1,000 | 1,234 | 0.6 | safe | 1,000 | **1,660x** |
| uniform20-s2 | 1,048,576 | 1,000 | 1,259 | 0.6 | safe | 1,000 | 1,627x |

mesh22 is the tightest at four times the range, and it is the graph that most
nearly refutes the rule: a 2048 x 2048 mesh has a shortest-path tree about 4094
hops deep, so the bound `depth * max_weight` does *not* fit in the histogram.
It fits in practice because shortest paths take cheap edges -- mesh22 averages
about 124 per hop against a maximum of 1000. That is a fact about the weight
distribution, not a guarantee, and the right reading of the last column is
"how much of the margin the graph would have to spend before the rule fails".

**The risk this rule carries is at the other end of the table**, and it is the
one that came in. A width of 1,000 against uniform20's range of 1,234 puts the
entire graph in the first two buckets, which is no admission control at all.
Before the measurement the only evidence on it was 7.6b's `width-1024` arm on
rmat22 -- the same over-width, 648x -- reading 0.98x to 1.00x, and I took that
as reason to expect over-width to be free. **It is not, and the default below
is `logv` because of it.**

`log(V)` survives as a floor. It binds only when the heaviest edge is smaller
than it -- essentially, unit-weight inputs, where spanning 2048 hops would leave
the buckets too coarse to order anything.

### Where the number comes from

Every graph-construction path already ended in a reduction to `Main::begin`,
carrying `max_sum`. **That value was dead**: `begin` handed it to
`SharedInfo::max_path`, which nothing ever read, and the comment claiming it
became `lmax` was wrong -- `lmax` is `numeric_limits<cost>::max()`. So the
reduction was a build barrier carrying nothing, and the repair changes its
operator from `sum_long` to `max_long` and its meaning from a dead sum to the
heaviest edge. No extra collective, no extra pass over the rows.

Getting the seed from Main to every chare needs one real barrier. It cannot
ride the `SharedInfo` broadcast that already carries the value: that is a group
proxy and `start_algo` goes to an array element, and Charm++ orders neither
against the other, so a chare could bin its first update at the provisional
width. `begin` therefore broadcasts `seed_bucket_width`, every chare sets the
width and contributes, and `start_source` -- which is the rest of what `begin`
used to do -- runs on that reduction. One broadcast and one empty reduction
against a setup phase measured in seconds, and `setup_time` now includes it,
which is correct: the solver could not have started sooner.

## The freeze, and the regression that proves it

The merge is exact for every ordinary bucket:
`floor(floor(x/s)/k) == floor(x/(s*k))`, so a count moved from bucket `i` to
`i/k` lands exactly where the later retirement decrement will look, on whichever
PE performs it. The clamp bucket was the only exception, and for one reason:
`coarsen_buckets` raised `bucket_limit` to `HISTO_BUCKET_COUNT * bucket_scale`,
so an update clamped at creation was *not* clamped at retirement and its
decrement landed in an ordinary bucket while its increment sat on 2047.

Freezing the threshold and leaving 2047 out of the merge closes it. The cost is
that coarsening no longer extends the representable range -- which is only
tolerable because the width rule means the range should not need extending.
**The two changes are one repair**; `--clamp-freeze on` with `logv` would be
correct and blind.

This is no longer an argument. `scripts/verify.sh` now carries a fixture that
fails without it:

```
40000 0 1 0 2 0.999 0.005 4 | --bucket-policy adaptive --bucket-width 8 \
                              --coarsen-clamped allow --clamp-freeze on
```

A 200 x 200 mesh at width 8 clamps at distance 16,384 against a range near
24,000, so the clamp bucket goes live while the band is still wide enough to
coarsen, and `--coarsen-clamped allow` takes the guard out of the way so the
merge itself is what is under test. Unfrozen, three runs out of three print
`COARSEN_CLAMPED`, report 65 stalls, hang until the 30-second timeout and
**fail verification against serial Dijkstra**. Frozen, the same three runs
coarsen once, print nothing, and converge in about 0.05 seconds.

That is the first direct demonstration that merging over a live clamp bucket
corrupts the histogram rather than merely being suspected of it, and it is also
the first time the failure has been reduced to something a gate can run in a
second. 7.6a's note could only say a 10,000-vertex mesh "never converges and
wrote a 574 MB log".

## The measurement: the rule is right about which graphs, and wrong as a default

Job 22061958, `--mode width`, all nine campaign graphs, one node, 120 workers,
540 timed runs, all valid, 56 minutes. `logv` -- the shipped behaviour
reproduced on the same binary -- is the baseline, so above 1 is faster.
`control` is the new defaults run a second time, and its spread against `weight`
is the floor below which nothing here is a result.

| graph | `logv` compute | `logv-frozen` | `weight` | work | rounds | sources won |
|---|---:|---:|---:|---:|---:|---:|
| road-ny | 2.467 s | 1.11x | **2.73x** | 0.06 | 1.03 | **4 / 4** |
| mesh22 | 5.179 s | 1.01x | 1.05x | 0.05 | 3.24 | 2 / 4 |
| mesh20 | 2.530 s | 0.99x | 1.01x | 0.04 | 3.01 | 1 / 4 |
| uniform20-s2 | 0.589 s | 1.06x | 0.93x | 1.25 | 1.13 | 2 / 4 |
| uniform20 | 0.594 s | 1.05x | 0.85x | 1.15 | 0.99 | 1 / 4 |
| rmat22 | 6.280 s | 1.22x | **1.4x slower** | 1.26 | 0.56 | 1 / 4 |
| youtube | 1.523 s | 0.82x | **1.7x slower** | 1.24 | 0.27 | 1 / 4 |
| rmat20 | 1.494 s | 0.98x | **2.4x slower** | 1.44 | 0.67 | 0 / 4 |
| rmat20-s2 | 1.289 s | 1.04x | **2.9x slower** | 1.65 | 0.83 | 0 / 4 |

Two results are unambiguous, and they point opposite ways. **road-ny gains 2.73x
on all four sources** -- larger than the 1.86x 7.6b measured, and the graph the
rule called worst served. **rmat20 and rmat20-s2 lose 2.4x and 2.9x on all four
sources**, with rmat22 and youtube losing on three of four. mesh20, mesh22,
uniform20 and uniform20-s2 are bimodal across sources and not resolvable here.

### Why, and it is in the bucket scale

| | mesh20 | mesh22 | road-ny | rmat20 | rmat20-s2 | rmat22 | uniform20 | uniform20-s2 | youtube |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `logv` scale | 1 | 1 | 1 | 8 | 7 | 7 | 10 | 9 | 8 |
| `weight` scale | 1 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |

**The graphs `weight` loses on are exactly the ones whose adaptive coarsening
already works.** They reach scale 7 to 10 unaided, so `log(V)` times that scale
is an effective width near 112 that the controller chose for itself, and the
rule overrides it with 1000. The graphs `weight` wins on are exactly the three
stuck at scale 1 -- which is the clamp race of 7.6b, and the only place the
controller cannot help itself.

So the diagnosis in `step76-controller.md` stands and sharpens: `log(V)` is not
wrong because it is small, it is wrong *only where coarsening is blocked*. What
is refuted is `max_edge_weight` as a global replacement.

### The allocation caveat, which cuts against the result too

This is one node at 120 workers. 7.6b measured the same over-width on rmat22 at
0.98x with 128 workers spread over eight nodes, and 1.00x at 16 workers on one.
Same PE count, opposite sign, so what changed is per-node worker density and not
the number of PEs -- an over-wide width admits everything at once, and 120
threads sharing one node is where that costs the most. Equally, 7.6b's mesh wins
of 1.27x to 1.67x were all at two nodes and above, and the meshes are flat here.
**Neither half of this table has been measured at the allocation where the other
half was.**

### The freeze is neutral, and stays on

`logv-frozen` reads 0.98x to 1.22x across the nine graphs, with delivered work
within 8% and rounds within 7% everywhere. The one loss, youtube at 0.82x, sits
inside that graph's own control spread, which is the widest in the campaign. It
still coarsens where `logv` does (scale 6 to 10 on the five graphs that
coarsen), because those graphs never needed the range extension the freeze gives
up. So the correctness repair is free, and `--clamp-freeze` defaults to `on`:
conservation now holds for every bucket and `--coarsen-clamped` guards nothing.

`--bucket-width-rule` defaults to `logv`. A rule that wins 2.73x on one graph
and loses 2.9x on two is not a default, and it was made the default before it
was measured, which was the wrong order.

## What is not claimed

The old behaviour and the new one both pass the full gate -- 18 configurations,
three on `sssp_smp_diag`, seven progress fixtures -- and produce identical
golden digests, which is what it means for a width to be a schedule and not an
answer. Nothing here is measured above one node.

## One harness addition

`benchmarks/run.py` now records `Bucket width`, `Bucket scale` and the heaviest
edge alongside the phase timers, so a run in the record says what it bucketed
with -- which nothing did before, and which is how a width derived from |V|
could bucket distances for a whole campaign without anyone reading the number.

## Taking the one gain that is unambiguous: road-ny, by name

The rule is not a default, but on road-ny it is worth 2.73x on 4/4 held-out
sources, and it is already a flag. So `benchmarks/run.py` carries a one-entry
map, `PER_GRAPH_WIDTH_RULE = {'road-ny': 'weight'}`, applied only to configs
that ask for it by setting `per_graph_width`. In `benchmark()` that is
`current` and `control` -- the shipped adaptive configuration and its
resolution floor, the pair that stands for ACIC as we would run it in the
comparison tables. The historical `old-fixed` and `open` arms are left alone
because changing them would change what an already-recorded arm means, and the
tuned-fixed arm already selects a width on the training sources, which makes
the rule inert anyway (`bucket_width_override > 0` skips it).

Named by graph, not derived. The derivation is the thing 7.6d refuted: there is
no cheap sound bound, `max_sum` is three orders too loose, and the one property
that does predict which graphs want it -- whether adaptive coarsening is
blocked at scale 1 -- is a runtime fact the rule cannot read at setup. A map
with a graph's name in it is honest about being a per-case tuning, and the
research claim this campaign is heading for (item 4) is *about* per-case tuned
references, so it needs one.

Two things keep it from going silent. The flag lands in `command` on every row,
and `bucket_width` parses back the width the run actually bucketed with, so no
row is ambiguous about which rule produced it. `--per-graph-width off` disables
the map for a campaign that wants an untuned baseline.

**Only road-ny is in the map.** mesh20 and mesh22 are the other two graphs
stuck at bucket scale 1, and the rule was built for them, but at 1.01x and
1.05x they are inside the campaign's own resolution. Putting them in would be
reading a tuning out of noise.

### One bug the default flip created, fixed here

Two arms of `--mode width` named `weight` and `weight-unfrozen` carried no
`--bucket-width-rule` flag: they inherited it from the binary default, which at
the time was `weight`. Commit 3977158 moved that default to `logv`, which would
have silently turned both into duplicates of the `logv` arms on the next run of
that mode. Every arm now states its own flags. `control` still carries none, by
design -- it *is* the default, so its meaning moves when the default moves, and
it now twins `logv-frozen` where in 22061958 it twinned `weight`.

## The node sweep, cut short by the cluster, and what survived

Jobs 22062931-34 re-take the width A/B at one, two, eight and sixteen nodes at
the deployment geometry (eight processes of fifteen). The Slurm controller
became unreachable about ninety minutes in. Compute jobs kept running, but
every `srun` inside them failed with `Unable to confirm allocation ... Unable
to contact slurm controller`, so 22062931 and 22062932 spent the rest of their
walltime producing failed launches -- 231 of 322 and 231 of 310 -- and
22062933 and 22062934 never started. The failures are infrastructure: not one of
them is a solver result.

When the controller returned, 22062933 and 22062934 were still *pending* --
they had never started -- so they were left alone rather than cancelled, and
they will run the campaign exactly as submitted, against the binary staged at
04d340b (sha a072a7f, recorded in `bin/width-manifest.txt` and on every row).
22063138 and 22063139 replace the ruined one- and two-node jobs against that
same binary. Nothing was restaged, because restaging under a pending job is
what `scripts/submit_width.sh` refuses to do and doing it by hand would be the
same mistake. Two consequences worth writing down: **`benchmarks/run.py` and
`benchmarks/launch_acic.sh` are read live from the working tree when a job
starts**, so neither may be edited while these are queued; and the 7.6e range
arm cannot be measured until they finish, since `logv-range` needs a binary
that has `--range-extend` at all.

What completed first is worth keeping, because rmat20 and uniform20 got their
full twelve runs per cell at both one and two nodes, and they are two of the
four graphs 7.6d recorded the width rule damaging. Speedups against
`logv-frozen`, paired medians, `control` being the same arm unflagged and so
the resolution floor:

| graph | nodes | pairs | `weight` | won | `control` (floor) | 7.6d at rpn 1 |
|---|---:|---:|---:|---:|---:|---:|
| rmat20 | 1 | 12 | **1.11x** | **11/12** | 1.06x slower | 2.4x slower, 0/4 |
| rmat20 | 2 | 12 | 1.02x | 7/12 | 1.02x slower | -- |
| uniform20 | 1 | 12 | 1.06x | 11/12 | 1.03x slower | 1.18x slower |
| uniform20 | 2 | 12 | **1.28x slower** | **0/12** | 1.01x slower | -- |
| mesh20 | 1 | 7 | 1.40x | 6/6 | 1.00x slower | 1.01x |
| mesh20 | 2 | 2 | 1.45x | 2/2 | 1.02x slower | -- |

These are paired: a run is compared only against the runs of the same graph,
source and repeat, which is not how the first pass through this data was read
and is why rmat20 moved from 1.16x to 1.11x. The paired count is the stronger
statement in any case. `control` is `logv-frozen` unflagged and reads 1.00x to
1.06x slower, which is the floor; rmat20's 1.11x median is not far outside it,
and 11 of 12 paired runs is.

**The rmat20 regression does not survive the geometry change.** 7.6d measured
`weight` at 2.4x slower on rmat20, losing every one of its four sources; at the
same node count and the same worker count, with those workers in eight
processes instead of one, it wins 11 of 12 paired runs at a median 1.11x. That is the second time in this
step that a width result has moved with the allocation rather than with the
graph, and the first time one has changed sign.

It does not rehabilitate the rule as a default. uniform20 goes the other way at
two nodes and does it cleanly -- 1.28x slower on 0 of 12 -- so the geometry
does not simply favour `weight`; it moves the result, in both directions. mesh22, rmat22, rmat20-s2, youtube and road-ny have no data at
all here, and the campaign has to be re-run. It does mean the table 7.6d
demoted the rule on was taken at one process of 120 workers, which the item-3
deployment campaign then measured at 7x to 20x off the best layout, and that
none of its numbers should be quoted without that written next to them.

## The one-node re-take, complete: every 7.6d regression is gone

Job 22063138, 324 of 324 valid, all nine graphs, one node, 120 workers, eight
processes of fifteen. Paired medians against `logv-frozen`, with the count of
paired runs won, and 7.6d's figure for the same graph at the same node and
worker count with those workers in **one** process:

| graph | `weight` | won | `control` (floor) | 7.6d at rpn 1 | won |
|---|---:|---:|---:|---:|---:|
| rmat20-s2 | 1.25x | 10/12 | 1.04x | **2.9x slower** | 0/4 |
| youtube | 1.24x | 11/12 | 1.05x | **1.7x slower** | 1/4 |
| mesh22 | 1.24x | 12/12 | 1.01x slower | 1.05x | 2/4 |
| mesh20 | 1.20x | 12/12 | 1.01x slower | 1.01x | 1/4 |
| rmat20 | 1.15x | 10/12 | 1.02x slower | **2.4x slower** | 0/4 |
| uniform20 | 1.10x | 11/12 | 1.01x slower | 1.18x slower | 1/4 |
| rmat22 | 1.06x | 8/12 | 1.03x slower | **1.4x slower** | 1/4 |
| uniform20-s2 | 1.05x | 9/12 | 1.03x slower | 1.08x slower | 2/4 |
| road-ny | 1.05x | 7/12 | 1.02x | **2.73x** | 4/4 |

**Not one graph regresses, and the four regressions 7.6d recorded -- 2.9x, 2.4x,
1.7x and 1.4x -- are all gone.** Six of the nine clear the control floor
comfortably; rmat22, uniform20-s2 and road-ny sit at it and are ties. The
ordering has also inverted: road-ny, which 7.6d measured as the rule's one
unambiguous win at 2.73x on 4/4 sources, is now its *weakest* result, and
mesh20 and mesh22, which 7.6d put at 1.01x and 1.05x and this note therefore
excluded from the per-graph map as noise, are 1.20x and 1.24x on 12 of 12.

The mechanism is not mysterious. road-ny's `logv-frozen` baseline is 0.204 s
here against about 2.5 s in 7.6d: the layout repair took twelve times off the
baseline, and the width rule's headroom went with it. What 7.6d was measuring
on road-ny was mostly the cost of running 120 workers in one process.

### What this does to the per-graph map

`PER_GRAPH_WIDTH_RULE = {'road-ny': 'weight'}` was justified by 2.73x on 4/4
sources, and at the deployment geometry that is 1.05x on 7 of 12 -- a tie. The
map names the one graph with the least to gain, and excludes the two with the
most, for reasons this data reverses. **It is left in place unchanged until the
two-, eight- and sixteen-node jobs report.** The rule this step exists to teach
is that a default must not move on one allocation, and that applies to
withdrawing one as much as to setting one: the two-node fragment already has
`weight` at 1.28x slower on uniform20 on 0 of 12, so "weight everywhere" is not
established either.

What *is* established is that the 7.6d table cannot be read as a property of
the graphs. It was a property of the process layout.

## Two nodes: the regressions come back, on different graphs

Job 22069453, 324 of 324 valid. Same three arms, same geometry, two nodes.
Paired medians against `logv-frozen`, with `control` -- the same arm unflagged
-- as the floor beside each one:

| graph | 1 node | won | 2 nodes | won | 2-node floor |
|---|---:|---:|---:|---:|---:|
| mesh20 | 1.20x | 12/12 | **1.47x** | 12/12 | 1.01x |
| mesh22 | 1.24x | 12/12 | **1.39x** | 12/12 | 1.00x slower |
| rmat20 | 1.15x | 10/12 | 1.13x | 10/12 | 1.01x |
| road-ny | 1.05x | 7/12 | 1.09x | 7/12 | 1.03x slower |
| youtube | 1.24x | 11/12 | 1.08x | 10/12 | 1.02x |
| rmat20-s2 | 1.25x | 10/12 | 1.01x slower | 6/12 | 1.02x |
| rmat22 | 1.06x | 8/12 | **1.16x slower** | 1/12 | 1.05x |
| uniform20-s2 | 1.05x | 9/12 | **1.35x slower** | 0/12 | 1.06x slower |
| uniform20 | 1.10x | 11/12 | **1.42x slower** | 2/12 | 1.13x slower |

So the one-node re-take did not establish that `weight` is safe; it established
that it is safe *at one node*. At two, uniform20-s2 loses on 0 of 12 and
uniform20 on 2 of 12, both beyond their own floors, and rmat22 loses on 1 of
12. The rule is not a default.

What survives both allocations is narrower and much more consistent than
anything 7.6d or the one-node table showed: **mesh20 and mesh22 win on 12 of 12
paired runs at both node counts, and the margin grows with scale** -- 1.20x to
1.47x and 1.24x to 1.39x. rmat20 is a smaller, steady 1.13-1.15x on 10 of 12.
road-ny is 1.05x and 1.09x on 7 of 12, which is a tie at both.

Note also that two nodes is the noisier allocation: uniform20's own control
reads 1.13x slower and uniform20-s2's 1.06x slower, against 1.00-1.05x at one
node. The uniform20 regression is outside its floor but not by much; the
uniform20-s2 one is smaller in the median and cleaner in the count.

### Which makes the per-graph map wrong in a second way

`PER_GRAPH_WIDTH_RULE` names road-ny, which is a tie at both node counts, and
excludes mesh20 and mesh22, which are the only two graphs that win everywhere
measured so far and win by more as the allocation grows. The map is still left
alone until eight and sixteen nodes report -- 7.6d was set on one allocation
and this note is not going to repeat that with two -- but the correction it
needs is now visible, and it is not the one this note proposed a few hours ago.

## Eight nodes, and what holds across three allocations

Job 22069454, 216 of 216 valid, two repeats rather than three so the counts
below are out of eight.

| graph | 1 node | 2 nodes | 8 nodes | sources won |
|---|---:|---:|---:|---|
| mesh20 | 1.20x | 1.47x | **1.56x** | 12/12, 12/12, 8/8 |
| mesh22 | 1.24x | 1.39x | **1.53x** | 12/12, 12/12, 8/8 |
| youtube | 1.24x | 1.08x | 1.31x | 11/12, 10/12, 7/8 |
| road-ny | 1.05x | 1.09x | 1.16x | 7/12, 7/12, 7/8 |
| rmat20 | 1.15x | 1.13x | 1.06x | 10/12, 10/12, 7/8 |
| rmat20-s2 | 1.25x | 1.01x slower | 1.35x | 10/12, 6/12, 8/8 |
| uniform20-s2 | 1.05x | 1.35x slower | 1.05x | 9/12, 0/12, 6/8 |
| rmat22 | 1.06x | 1.16x slower | 1.01x slower | 8/12, 1/12, 3/8 |
| uniform20 | 1.10x | 1.42x slower | 1.08x slower | 11/12, 2/12, 1/8 |

**mesh20 and mesh22 win at every allocation on every source -- 28 of 28 paired
runs each -- and the margin grows monotonically with node count.** That is the
cleanest signal this project has produced, and it is the one result here that
does not depend on which allocation it was taken at.

At the other end, uniform20 is the only graph that regresses consistently, and
rmat22 is a tie or worse at both multi-node points. rmat20 declines steadily as
nodes grow, 1.15x to 1.06x, and is a mild win throughout.

rmat20-s2 and uniform20-s2 change sign between allocations, and their
eight-node control floors are 1.10x and 1.12x on only eight pairs. Those two
are noise at this sample size and are not evidence for anything.

The shape of the answer is therefore not the one 7.6d proposed, nor the one the
one-node re-take suggested. The width rule is not a default, and it is not a
road-ny rule. It is a **mesh** rule, and road-ny -- the graph 7.6d measured at
2.73x and this note put in the map on that basis -- is the weakest consistent
win in the table.

## Sixteen nodes, and the answer

Job 22069455, 216 of 216 valid. `weight` wins on all nine graphs here, 1.18x to
1.93x — but sixteen nodes is also the noisiest allocation in the sweep, with
control readings from 1.18x to 1.15x slower, a band of about 1.35x, so the
margins matter less than the counts.

All four allocations, paired speedups against `logv-frozen`, and the paired
runs the rule won out of forty:

| graph | 1 node | 2 nodes | 8 nodes | 16 nodes | won |
|---|---:|---:|---:|---:|---:|
| **mesh20** | 1.20x | 1.47x | 1.56x | 1.93x | **40/40** |
| **mesh22** | 1.24x | 1.39x | 1.53x | 1.67x | **40/40** |
| youtube | 1.24x | 1.08x | 1.31x | 1.25x | 34/40 |
| rmat20 | 1.15x | 1.13x | 1.06x | 1.59x | 34/40 |
| rmat20-s2 | 1.25x | 1.01x slower | 1.35x | 1.46x | 32/40 |
| road-ny | 1.05x | 1.09x | 1.16x | 1.18x | 28/40 |
| uniform20-s2 | 1.05x | 1.35x slower | 1.05x | 1.22x | 23/40 |
| uniform20 | 1.10x | 1.42x slower | 1.08x slower | 1.38x | 22/40 |
| rmat22 | 1.06x | 1.16x slower | 1.01x slower | 1.42x | 19/40 |

1080 timed runs, every one valid, four allocations, one binary.

**Only mesh20 and mesh22 win every allocation on every source, and only they
grow monotonically with node count.** They are the map. The next three never
regress, but are weaker and do not clear their own control floors everywhere;
the last three change sign between allocations. None of those six is
established, and the honest reason is that this sweep cannot separate a 1.1x
effect from a 1.15x control floor at eight paired runs.

It is not a default. At two nodes four graphs regress, one of them by 1.42x on
2 of 12.

`PER_GRAPH_WIDTH_RULE` is now `{'mesh20': 'weight', 'mesh22': 'weight'}`. It
named road-ny, alone, because 7.6d measured road-ny at 2.73x on 4/4 sources and
put mesh20 and mesh22 at 1.01x and 1.05x — inside that campaign's resolution,
and so excluded as noise. At a deployable process layout road-ny is the weakest
consistent win in the table and the two mesh graphs are the entire result. Both
of the map's original decisions were wrong, and they were wrong for the same
reason: **the 7.6d table was a property of the process layout, not of the
graphs.**

### What is still not known

Every number here is compute time at eight processes of fifteen. At eight nodes
setup is 1.085 s against a compute median of 0.216 s, so a growing share of
these runs is process launch and graph reading — and these graphs are small for
sixteen nodes, about a thousand vertices per worker on mesh20. The rule's
margin grows with node count on the mesh graphs, which is the interesting part,
but whether that survives a problem sized for the allocation is untested.
