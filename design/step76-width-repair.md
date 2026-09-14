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

**The risk this rule carries is at the other end of the table.** A width of
1,000 against uniform20's range of 1,234 puts the entire graph in the first two
buckets, which is no admission control at all. The one measurement that bears on
it is 7.6b's `width-1024` arm on rmat22 -- the same over-width, 648x -- which
read 0.98x to 1.00x at every allocation. That is evidence, and it is evidence
from a different graph; rmat20, rmat20-s2, uniform20 and uniform20-s2 have never
had their width varied, and they are the reason `--mode width` runs over all
nine graphs rather than the three that motivated the repair.

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

## What is not claimed

The old behaviour and the new one both pass the full gate -- 18 configurations,
three on `sssp_smp_diag`, seven progress fixtures -- and produce identical
golden digests, which is what it means for a width to be a schedule and not an
answer. No timing claim is made here. The prior is 7.6b's `width-1024` arm,
which is what `weight` reduces to on the eight graphs whose weights top out at
1000, and which won 1.27x to 1.67x on mesh20 across two, four, eight and
sixteen nodes without losing a race in 64 runs. Whether this implementation of
it behaves the same, and what it costs on the four graphs the rule calls safe,
is what `--mode width` measures.

## One harness addition

`benchmarks/run.py` now records `Bucket width`, `Bucket scale` and the heaviest
edge alongside the phase timers, so a run in the record says what it bucketed
with -- which nothing did before, and which is how a width derived from |V|
could bucket distances for a whole campaign without anyone reading the number.
