# Step 7.6e: the three stuck graphs are not stuck on coarsening

The 7.6d measurement left one option sharper than the rest. `--bucket-width-rule
weight` wins only on mesh20, mesh22 and road-ny, and those are exactly the three
graphs whose adaptive coarsening never leaves bucket scale 1, while the five
that reach scale 7 to 10 unaided are the ones it damages by up to 2.9x. The
obvious reading was that coarsening is blocked on those three and that
unblocking it would buy what the width rule buys, without the cost. That reading
is wrong, and the diagnostic that says so is four lines of the round trace the
7.6d campaign already wrote.

## What the refusal reasons say

Every round records why it did or did not coarsen. Over the nine diagnostic runs
of job 22061958, on the three stuck graphs:

| graph | arm | rounds | TWO_TIER | BAND_NARROW | MERGED | CLAMP_LIVE |
|---|---|---:|---:|---:|---:|---:|
| mesh20 | logv-frozen | 144 | 117 | 26 | 0 | **0** |
| mesh22 | logv-frozen | 291 | 244 | 46 | 0 | **0** |
| road-ny | logv-frozen | 160 | 148 | 11 | 0 | **0** |

Not one round on any of them was refused because of the clamp. The clamp guards
that 7.6d spent its effort on were never what stopped these graphs coarsening.
They were stopped by having too little in flight to describe a distribution, and
otherwise by a band already narrower than twice the target -- which is the
controller correctly answering the question it was asked. Merging adjacent
buckets makes a frontier easier to describe. These frontiers were already easy
to describe. Nothing was gained by merging them, and the controller said so.

## What is actually wrong

The same traces say it in one column. Under `logv`, road-ny reaches
`first_nonzero = 2047` by round 40 and stays there for the remaining 120 rounds:
one occupied bucket, span 1, every live update in the overflow slot. mesh22 does
the same from round 72 of 291. The controller is not mis-tuning the frontier; it
has no frontier to tune. Everything is in one bucket, so there is no ordering
left to schedule by, and the run degenerates:

| graph | arm | updates created | rounds | end state |
|---|---|---:|---:|---|
| road-ny | logv | 103,567,601 | 160 | 120 rounds pinned at 2047 |
| road-ny | weight | 2,297,686 | 188 | frontier at bucket 22 |
| mesh22 | logv | 714,697,442 | 291 | 219 rounds pinned at 2047 |
| mesh22 | weight | 23,065,073 | 859 | frontier at bucket 351 |

**45x and 31x the updates.** Note that `weight` takes *more* rounds on both and
is still faster: this was never a round-count result. It is a work result, and
the work is the price of running most of a solve with no priority order.

The cause is range, not resolution. The histogram spans `2048 * width`
distance units. Under `logv`, road-ny's width is 12.49 and its distances run to
about 1.29e6, which is 103,000 buckets' worth of distance in a structure with
2048 of them. Under `weight` the width is the heaviest edge and the whole graph
fits. That is the entire difference, and it explains both halves of the 7.6d
result at once: `weight` wins where `logv` runs out of range and loses where it
does not, because there the only thing a width of 1000 buys is a graph whose
every vertex is in the first two buckets.

## Why the three repairs are not interchangeable

Range is `bucket_count * width`. There are exactly three ways to get more of it.

**Raise the width.** That is `--bucket-width-rule weight`, measured in 7.6d,
kept as a per-case setting because it costs 1.4x to 2.9x on the graphs that do
not need it.

**Raise the bucket count.** Blocked, and by more than the `pq_hold` reservation
7.6d recorded. htram allocates `destCount() * histo_bucket_count` `std::queue`
objects per PE, and a libstdc++ deque allocates its first node eagerly, so the
holds already cost about 576 bytes per bucket per destination per PE -- at eight
processes over sixteen nodes that is 151 MB per PE before a single update is
held. Worse than the memory, the scans: `process_heap()` walks buckets 0 through
`heap_threshold` on every call and htram's filler path walks
`tram_threshold + 1` through the top. Both are O(bucket count) on a hot path.
Raising 2048 to the 131,072 road-ny would need is not a constant change; it is a
redesign of both hold structures into something sparse.

**Raise the clamp during the run.** This is what `coarsen_buckets` did before
7.6d, by raising `bucket_limit` alongside `bucket_scale`, and it is the bug the
clamp freeze closed. The histogram is incremented by the PE that creates an
update and decremented by the PE that retires it, and commit 1a46a9e establishes
that no PE can walk "its" live updates -- an update in a TRAM buffer or on the
wire is in no enumerable structure -- so re-binning is out and every merge has to
land the decrement where the increment went. Ordinary buckets do that on their
own: `floor(floor(x/s)/k) == floor(x/(s*k))`. The overflow slot does not, because
it is not an index: it means "past the clamp", and if the clamp moves between
creation and retirement the retiring PE computes a real bucket while the
increment sits on 2047, stranded where nothing will ever take it away.

It is worth saying why the obvious way round that fails. One could try to extend
only while the overflow slot is provably empty, bounding the furthest distance
any PE could create as (highest threshold ever broadcast + 1) * scale + the
heaviest edge. That bound is not sound here: htram's `ADD_FILLERS` path ships
held updates from *above* the threshold, up to and including the overflow slot,
so processing a filler creates distances the threshold says nothing about. The
clamp can fill in any round, whatever the frontier is doing.

## The repair: one bit, at creation

`UPDATE_OVERFLOW_BIT`, bit 62 of `Update::dest_vertex`. An update charged to the
overflow slot when it was created is flagged, and every decrement of the
histogram goes through `bucket_of()`, which returns the overflow slot for a
flagged update whatever the clamp has since become. The increment and the
decrement then always agree, and `bucket_limit` is free to rise.

The bit is carried in the top of the vertex field rather than in a field of its
own because `Update` is the payload of every htram message. A field would take
it from 16 to 24 bytes and turn every A/B against a recorded run into a
comparison of message sizes. Vertex ids here are under 2^32 against a ceiling of
2^62, and `-1`, which `fold_batch` uses as a tombstone, is not a taggable value.
Routing, batch folding and the combine hold all key on `update_vertex()`, which
masks it off.

`--range-extend` then merges by k and raises the clamp by the same k: the merge
has just freed the top half of the index space, so it is spent on distance
instead and every index stays in range. It composes with the freeze rather than
undoing it -- a fixed overflow index is exactly what makes a creation-time flag
mean anything -- and it is off in a VCOUNT build, where vcount is indexed by a
vertex's *current* distance and re-derived at each change, so nothing carries a
creation-time bucket for it.

### The trigger, and the one way it first went wrong

The controller asks a different question from the band test, and asks it whatever
the band and the two-tier branch say, because both of those are true and beside
the point when the range is short. The first version read the standing overflow
count: extend while the overflow slot holds an eighth of the live population.
That runs away. A flagged update retires from the overflow slot by construction,
so the standing count does not fall when the clamp rises; the controller saw its
own raise do nothing and raised again every round. road-ny reached bucket scale
1,048,576 -- an effective width of 13 million against a graph 1.3 million wide,
which is one bucket again by the other door, and it was slower than not
extending at all.

The fix is to read the *arrival* rate instead. The reduction carries a second
overflow number, the count ever charged there, and its round-over-round
difference is the only thing that says whether the range is still too small.
When a raise is finally enough, arrivals stop and so does the rule -- it needs
no separate stopping condition, and the `bucket_scale < 2^20` cap is a backstop,
not the mechanism.

### And one the trigger did not cause

road-ny, with the arrivals trigger in place, then ran to the timeout and
printed a wrong digest. The cause is a line that predates this work by a
commit. When the controller merges buckets it divides every bucket index it is
holding by k -- the two thresholds, the BFS threshold, and the window origin --
because a merge moves every index. Under the freeze one index does not move:
the overflow slot stays at 2047, which is the entire content of the freeze. So
a window origin sitting on the overflow is divided to 2047/k, where by
construction nothing is, and the controller waits there for a frontier that
cannot arrive. A threshold sitting on it is closed the same way, and the
overflow stops draining.

This is the stranding the clamp freeze was written to prevent, one level up:
7.6d fixed it in the histogram and left it in the controller, where nothing
reached it, because the graphs that coarsen never have anything in the overflow
and the graphs with something in the overflow never coarsened. Range extension
is the first thing that does both at once. Indices equal to the overflow slot
are now left alone under the freeze.

## Where it stands

Local eight-PE runs, one source per graph, three repeats, against the recorded
digest. A login node is not a measurement instrument -- see the leak below --
so times are reported only as a sanity direction. Updates created, extension
counts and digests are what these runs establish.

| graph | ext | scale | updates created, off | on | digest |
|---|---:|---:|---:|---:|---|
| mesh20 | 3 | 200-208 | 18.7-18.9 M | 6.9-12.0 M | matches |
| mesh22 | 5 | 640 | 39.1-45.7 M | 27.9-29.4 M | matches |
| road-ny | 6 | 3328-6400 | 6.8-7.2 M | 2.1-6.0 M | matches |
| youtube | **0** | 8 | 6.3-6.5 M | 6.3-6.4 M | matches |
| rmat20 | **0** | 8 | 34.1-34.4 M | 34.0-34.5 M | matches |
| uniform20 | **0** | 10-11 | 33.6 M | 33.6 M | matches |

The zeroes are the result. On the five graphs 7.6d showed the width rule
damaging, the range is already adequate, nothing is ever charged to the
overflow slot, the rule never fires, and the runs are the unextended runs --
same scale, same work, same digest. That is what the width rule could not do:
it is a setting, so it applies whether or not the graph needs it, and on rmat20
it cost 2.4x. This is a condition, so it applies where the condition holds.

On the three, the work falls by 1.4x to 3.4x. Times are not quoted: on this
node the same cell varies by more than the effect, and mesh20 `off` read 4.7 s
in one early run and 0.27 s in another. **The thing this establishes is that
the mechanism is sound, stable over repeats, and silent where it should be.
Whether it is worth anything on a clock is a cluster question and is not
answered here.** Note also that road-ny's pathology is much milder at 8 PEs
(7 M updates unextended) than at the campaign's 120 (103 M), so the local
numbers understate what there is to win.

`--range-extend` defaults to **off**. It fires zero times on a graph already in
range: at width 200 on the fixture mesh, on and off agree on bucket scale,
coarsenings and extensions, and differ by less than the run-to-run spread on
time. So a default-on argument is available. 7.6d made a width rule the default
on exactly that kind of argument and then measured it losing 2.9x, and the
order of those two steps was the mistake. This ships as a flag with a campaign
arm, `logv-range`, behind it in `--mode width`.

## Two ways the first version was wrong, and how each was caught

**The denominator.** The trigger first asked whether the overflow slot held an
eighth of the *live* population. That is wrong in both directions: early, most
of what is live was created before this round, so the share understates; late,
the live population drains to almost nothing, so a handful of arrivals reads as
a range emergency and the rule doubles a scale that is already wide enough. On
mesh20 the difference between one run and the next was 4 extensions against a
run that had not finished in 250x the time -- and the first sweep of mesh20 got
the good draw, so a single run of each cell would have shipped it. The
denominator is now the round's own creations: of the updates made this round,
what share was out of range. A one-round cooldown goes with it, because a chare
applies an extension on the broadcast after the reduction it was decided from,
so the next round's arrivals were partly charged under the old clamp and would
buy a doubling already bought.

**The standing count.** Before that, the trigger read the overflow slot's
standing occupancy, which does not fall when the clamp rises -- a flagged
update retires from the overflow slot by construction. The controller saw its
own raise do nothing and raised again every round; road-ny reached bucket scale
1,048,576, an effective width of 13 million against a graph 1.3 million wide,
which is one bucket again by the other door and slower than not extending at
all.

## And one the trigger did not cause

road-ny then ran to the timeout and printed a wrong digest. The cause is a line
that predates this work by a commit. When the controller merges buckets it
divides every bucket index it is holding by k -- the two thresholds, the BFS
threshold, and the window origin -- because a merge moves every index. Under
the freeze one index does not move: the overflow slot stays at 2047, which is
the entire content of the freeze. So a window origin sitting on the overflow is
divided to 2047/k, where by construction nothing is, and the controller waits
there for a frontier that cannot arrive. A threshold sitting on it is closed
the same way, and the overflow stops draining.

This is the stranding the clamp freeze was written to prevent, one level up:
7.6d fixed it in the histogram and left it in the controller, where nothing
reached it, because the graphs that coarsen never have anything in the overflow
and the graphs with something in the overflow never coarsened. Range extension
is the first thing that does both at once. Indices equal to the overflow slot
are now left alone under the freeze.

## The leak, which is a trap and not a curiosity

The pre-fix hang hit `--timeout 180`, printed its (wrong) digest, and then did
not exit: sixteen minutes later it was still burning eight cores at 635% CPU,
next to every local timing taken in that window. `benchmarks/run.py` signals the
whole process group on a timeout, so a campaign is not exposed to this, but a
local run is, and the first mesh20 numbers written down here -- a 10x that was
really about 1.2x -- were taken beside one.

## The gate fixture

`scripts/verify.sh` grows an eighth progress fixture: the same 200x200 mesh as
the clamp fixture, binned at width 1, which puts every distance past the clamp
so the overflow slot takes essentially the whole run and the rule has to raise
the clamp a dozen times to recover an ordering. Three things are on test and
each has already failed once -- the creation-time flag, the arrivals trigger,
and keeping the overflow index out of the rescale. A PASS is all three.

## Measured: the extension fails at scale, on exactly the graphs it was for

Jobs 22071815-18, one binary (`457ddc10`), four arms
(`logv-frozen,logv-range,weight,control`), eight processes of fifteen per node,
four sources, at one, two, eight and sixteen nodes. Three of the four jobs hit
their walltime: the walltimes in `submit_width.sh` were sized from a
three-arm campaign and this one ran four, which is a third more work. The
eight-node job finished; one node covered five graphs, two nodes eight, and
eight and sixteen nodes all nine. No conclusion below rests on a graph that
only one allocation reached.

`logv-range` against `logv-frozen`, paired by (graph, source, rep), with hangs
counted separately:

| graph | 1 node | 2 node | 8 node | 16 node |
|---|---|---|---|---|
| mesh20 | 1.21x 8/11, 2 hung | 1.23x 5/8, 5 hung | **all 9 hung** | **all 9 hung** |
| mesh22 | 1.23x slower, 1 hung | 1.16x slower, 3 hung | 8 of 9 hung | **all 9 hung** |
| road-ny | - | 1.19x 5/5, 4 hung | 1.12x slower 1/9 | 1.21x slower 0/9 |
| rmat20 | 1.04x | 1.09x slower | 1.07x | 1.04x slower |
| rmat20-s2 | - | 1.04x slower | 1.08x slower | 1.02x slower |
| rmat22 | 1.05x | 1.01x slower | 1.04x | 1.01x slower |
| uniform20 | 1.06x slower | 1.05x | 1.01x slower | 1.05x |
| uniform20-s2 | - | 1.00x | 1.09x | 1.01x |
| youtube | - | - | 1.07x slower | - |

Read the bottom six rows first. Those are the graphs the local sweep showed
extend zero times, and the arm is a no-op on them to within the resolution
floor, which is what it should be. The arm is doing nothing where it is not
needed, so the plumbing is right.

Then read the top three. mesh20 and mesh22 are the graphs 7.6e was built for
and the arm hangs on them at every allocation, totally at eight and sixteen
nodes: 35 of 36 runs. road-ny, the third stuck graph, hangs four times at two
nodes and where it survives at eight and sixteen it is 1.12x and 1.21x
*slower*, winning 1 of 18 paired runs.

The two cells that look like wins are not. mesh20 reads 1.21x at one node and
1.23x at two -- out of cells that also hung twice and five times. The ratio is
over the runs that finished, and the runs that finished are the ones that
extended least. That is selection, not a speedup, and quoting it would be the
same error as quoting a mean over the survivors of a crash.

**The verdict is that `--range-extend` does not ship.** It stays in the tree,
defaulted off, with this note attached, because the diagnosis it rests on --
the three graphs are out of range, not blocked on coarsening -- is still the
right diagnosis and still needs a repair. What is now known is that raising the
clamp mid-run is not that repair, and the local sweep that said otherwise (8
PEs, one process, every digest matching, updates cut 2-3x) measured a
configuration with no broadcast skew worth the name. A single-process sweep
cannot qualify a change whose whole risk is that two chares disagree.

## The deferral guard never fires

The fix in `c07dc22` for the broadcast/point-to-point race -- detect an update
created beyond my clamp, hold it, drain after the rescale -- is dead code in
practice. Across 11,970 stall reports at one node and 150,184 at eight, every
one reads `deferred=0`, `deferred_peak=0`, `deferred_total=0`.

The guard asks one question:

```cpp
bool created_beyond_my_clamp(const Update &u) {
  return !update_overflowed(u) &&
         (double)u.distance * bucket_multiplier >= bucket_limit;
}
```

which is about `bucket_limit` alone. But `coarsen_buckets(k, extend)` moves two
things, not one:

```cpp
bucket_scale *= k;                 // always
if (!clamp_freeze)      bucket_limit = HISTO_BUCKET_COUNT * bucket_scale;
else if (extend)        bucket_limit *= k;
```

So an extend is a coarsen *and* a clamp raise, and two chares that disagree can
disagree about the scale as well as the limit. Ordinary coarsening survives
scale skew because of the merge identity -- a lagging chare's decrement at
`b/s` migrates to `b/(s*k)` when it catches up, exactly where the leading
chare's increment went. Whether that identity still covers the pair
(scale moved, limit moved) is the question this guard never asks, and the
measurement cannot answer it either way, because the guard it would have to be
compared against never ran.

That is the honest state: the race was fixed against a hypothesis, the
hypothesis was not tested, and the arm fails for a reason that is still open.
