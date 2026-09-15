# The shipped default deadlocks, and item (1) is not closed

Item (1) of the stopping rules is progress repair, with the rule attached that
*a watchdog that forces an exit is not a repair*. It was closed earlier in
step 7. Job 22071815 reopens it, and the evidence is not about `--range-extend`
at all.

## What was seen

Of 253 runs at one node, eight did not finish. Five of them are baseline arms:
three `logv-frozen` and two `control`, all on mesh22, all with `--range-extend`
off and therefore running the configuration that ships today. (The other three
are the range arm, and belong to `step76-range.md`.)

The runs do not run slowly. They stop. `PROGRESS_STALL` is emitted repeatedly
over the life of each hung run, and every counter in it is bit-identical from
the first report to the last:

```
run2  t 0.1 -> 114.8  live=5973  clamped=5969  first_nonzero=511  heap_thr=511  occupied=1
run3  t 0.1 ->  59.1  live=5639  clamped=5629  first_nonzero=204  heap_thr=204  occupied=1
run4  t 0.1 ->  58.8  live=5618  clamped=5613  first_nonzero=157  heap_thr=157  occupied=1
run6  t 0.1 ->  60.1  live=6592  clamped=6573  first_nonzero=170  heap_thr=170  occupied=1
run7  t 0.1 -> 117.8  live=4955  clamped=4937  first_nonzero=682  heap_thr=682  occupied=1
```

`live`, `window_sum`, `clamped`, `first_nonzero` and the thresholds do not move
by one across two minutes. The controller keeps turning -- it is the thing
counting `rounds_without_progress` -- but nothing underneath it moves.

These are the unextended stalls and they can be told apart from the range arm's
without ambiguity, because only the extend path raises `bucket_limit`. At one
node 7,438 stall lines carry `bucket_limit=2048.000000`, its initial value. At
eight and sixteen nodes there are none: every stall line there has a raised
limit, which is why only the range arm failed at those allocations.

## The part of the diagnosis that is solid

`clamped` is within twenty of `live` in five of the eight, and `occupied=1`.
Nearly every update still alive is above the window, in the overflow slot, and
exactly one bucket inside the window holds anything at all.

That is 7.6e's range diagnosis reaching its end state. Under `clamp_freeze`,
`bucket_limit` is set once to `HISTO_BUCKET_COUNT` and then never again for a
run that does not extend, while `bucket_scale` multiplies on every coarsen --
here to 3, 4, 10, 12 and 13. So the set of distances that miss the overflow
slot is frozen at `distance < 2048 * width` for the whole run, and coarsening
does not widen it; it only compresses the distances that are already in range
into `2048 / bucket_scale` indices. Once the frontier passes the clamp, the
live population goes to the overflow slot, which is excluded from merges, and
stays.

What 7.6e recorded as *ordering is lost* is therefore too mild. The terminal
case is not a slow run. It is a run that stops, and it happens in the shipped
default.

## The part that is not solid, and must not be guessed

The obvious explanation is that the work is stranded above the admission
threshold. It is not, and the logs say so plainly. Taking every per-PE stall
report and asking whether that PE's lowest live bucket is at or below its own
heap threshold:

| | 1 node | 8 nodes |
|---|---|---|
| PE has no live work | 8,315 | 68,103 |
| live work **at or below** threshold, i.e. admissible | 3,386 | 73,537 |
| live work above threshold, waiting | 269 | 8,544 |

The common case in a stall is a PE holding work it is already allowed to run.
Summing over those PEs at eight nodes: **11.6M updates in `pq`**, the ready
queue, and 9.9M in `pq_hold`, with `tram_admitted=0` and `tram_buffered=0`.

So the last step fails, not the admission step. Work is ready, permitted, and
in the queue, and the queue is not drained. Whether that is a chare that has
returned from `process_heap` and is waiting on a re-trigger that no longer
comes, a termination-detection interaction, or a genuine lost message, this
data cannot say, and there is no honest way to narrow it from log archaeology.

## What would settle it

A reproducer, not another campaign. The fixture wanted is the one
`scripts/verify.sh` already almost has: a mesh binned at a width narrow enough
that the frontier passes the clamp, run to the stall, with the drain path
instrumented -- what the chare was last told, what it did with `pq`, and
whether a trigger arrived. The condition is cheap to force (`--bucket-width 1`
on a 200x200 mesh puts every distance past the clamp) and reproduced five times
in 253 runs at one node without being forced at all.

Note what this does and does not disturb. It does not touch the width result:
the `weight` arm did not hang once in this campaign at any allocation, and
`mesh20`/`mesh22`, the two graphs the per-graph map names, are the two where
`weight` keeps the frontier inside the clamp and so never reach this state.
The mechanism is in fact an argument *for* the map. But a default that can stop
is a defect of the default, and it is item (1), not item (3).

## Why 1,080 earlier runs did not show it

They did not show it at all: the three prior width campaigns emit zero
`PROGRESS_STALL` lines between them. The source is identical -- no tracked file
changed between the two builds, `sssp_smp.cpp` last moved in `c07dc22` which
predates both, and htram last moved before that -- so this is not a regression
introduced by the range work. It is an intermittent failure that 1,080 runs
happened to miss and 253 happened to hit, which at five events in roughly
twenty-six eligible baseline runs is a rate high enough that missing it
twenty-six times running was luck.

The lesson for the campaign, not just the bug: every one of those 1,080 runs
was scored valid, and validity was doing more work than it looked like. A
campaign that reports only medians over valid runs cannot distinguish "this
configuration is fine" from "this configuration stops sometimes and we did not
draw one". The hang rate belongs in the table beside the speedup.
